/* Copyright (c) 2008, 2009, 2010, 2011, 2012 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_MLOCKALL
#include <sys/mman.h>
#endif

#include "bridge.h"
#include "command-line.h"
#include "compiler.h"
#include "daemon.h"
#include "dirs.h"
#include "dpif.h"
#include "dummy.h"
#include "leak-checker.h"
#include "memory.h"
#include "netdev.h"
#include "openflow/openflow.h"
#include "ovsdb-idl.h"
#include "poll-loop.h"
#include "process.h"
#include "signals.h"
#include "simap.h"
#include "stream-ssl.h"
#include "stream.h"
#include "stress.h"
#include "svec.h"
#include "timeval.h"
#include "unixctl.h"
#include "util.h"
#include "vconn.h"
#include "vlog.h"
#include "lib/vswitch-idl.h"
#include "worker.h"

VLOG_DEFINE_THIS_MODULE(vswitchd);

/* --mlockall: If set, locks all process memory into physical RAM, preventing
 * the kernel from paging any of its memory to disk. */
static bool want_mlockall;

static unixctl_cb_func ovs_vswitchd_exit;

static char *parse_options(int argc, char *argv[], char **unixctl_path);
static void usage(void) NO_RETURN;

/**
 * The main function.
 * @param argc: Number of args
 * @param argv[]: arg vector
 * @return: 0 if success
 */
int
main(int argc, char *argv[])
{
    char *unixctl_path = NULL;
    struct unixctl_server *unixctl;
    struct signal *sighup;
    char *remote;
    bool exiting;
    int retval;

    proctitle_init(argc, argv); //copy argvs from its orginal location
    set_program_name(argv[0]);
    stress_init_command(); //register stress/* cmds to the commands (struct unixctl_command)

    /*remote stores the ovsdb sock, unixctl stores the ctl socket for ovs-app.
     * ovsd works as a client to receive cmds from servers, eg, ovs-appctl. */
    remote = parse_options(argc, argv, &unixctl_path); 

    signal(SIGPIPE, SIG_IGN); //ignore the pipe read termination signal
    sighup = signal_register(SIGHUP); //register the SIGHUP signal handler, ignore terminal close.
    process_init(); //register the handler for child termination signal

    /*init the ovs configuration tables. */
    ovsrec_init();

    /*daemonize the process. */
    daemonize_start(); 

    /*lock all mem into RAM. */
    if (want_mlockall) {
#ifdef HAVE_MLOCKALL
        if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
            VLOG_ERR("mlockall failed: %s", strerror(errno));
        }
#else
        VLOG_ERR("mlockall not supported on this system");
#endif
    }

    /*start a worker subprocess, call worker_main (receive data and process), init a pipe: parent send data?*/
    worker_start(); 

    /*create a punixctl server (&unixctl) listening on unixctl_path (ovswitchd.pid.ctl) to get cmd from ovs-apps.*/
    retval = unixctl_server_create(unixctl_path, &unixctl);
    if (retval) {
        exit(EXIT_FAILURE);
    }
    unixctl_command_register("exit", "", 0, 0, ovs_vswitchd_exit, &exiting);

    /*init the ovsdb's configurations, and register unix ctl cmds: qos, bridge. */
    bridge_init(remote);
    free(remote);

    exiting = false;
    while (!exiting) {
        worker_run(); //process the RPC reply from the worker subprocess, call its cb_reply callback
        if (signal_poll(sighup)) {
            vlog_reopen_log_file();
        }
        memory_run();//monitor the memory
        if (memory_should_report()) {
            struct simap usage;

            simap_init(&usage);
            bridge_get_memory_usage(&usage);
            memory_report(&usage);
            simap_destroy(&usage);
        }

        /*process data pkts from the datapath*/
        /*check each bridge and handle upcalls from dp.
          by calling it's ofproto_class->run_fast(). */
        bridge_run_fast(); 
        //main process part, handling of cmds and db updates.
        bridge_run(); 
        bridge_run_fast(); //could be run to check the bridge multi-times

        /*connect to ovswitchd.pid.ctl, via which, accept cmds from ovs-appctl. */
        unixctl_server_run(unixctl); 

        /*perform the run() of each class in netdev_classes if open any netdev. */
        netdev_run(); 

        worker_wait(); //poll loop to wake up if there's RPC replies
        signal_wait(sighup);
        memory_wait();
        bridge_wait();
        unixctl_server_wait(unixctl);
        netdev_wait();
        if (exiting) {
            poll_immediate_wake();
        }
        poll_block();
    }
    bridge_exit();
    unixctl_server_destroy(unixctl);
    signal_unregister(sighup);

    return 0;
}

static char *
parse_options(int argc, char *argv[], char **unixctl_pathp)
{
    enum {
        OPT_PEER_CA_CERT = UCHAR_MAX + 1,
        OPT_MLOCKALL,
        OPT_UNIXCTL,
        VLOG_OPTION_ENUMS,
        LEAK_CHECKER_OPTION_ENUMS,
        OPT_BOOTSTRAP_CA_CERT,
        OPT_ENABLE_DUMMY,
        OPT_DISABLE_SYSTEM,
        DAEMON_OPTION_ENUMS
    };
    static struct option long_options[] = {
        {"help",        no_argument, NULL, 'h'}, //--help
        {"version",     no_argument, NULL, 'V'}, //--version
        {"mlockall",    no_argument, NULL, OPT_MLOCKALL}, 
        {"unixctl",     required_argument, NULL, OPT_UNIXCTL}, //--unixctl 
        DAEMON_LONG_OPTIONS,
        VLOG_LONG_OPTIONS,
        LEAK_CHECKER_LONG_OPTIONS,
        STREAM_SSL_LONG_OPTIONS,
        {"peer-ca-cert", required_argument, NULL, OPT_PEER_CA_CERT},
        {"bootstrap-ca-cert", required_argument, NULL, OPT_BOOTSTRAP_CA_CERT},
        {"enable-dummy", optional_argument, NULL, OPT_ENABLE_DUMMY},
        {"disable-system", no_argument, NULL, OPT_DISABLE_SYSTEM},
        {NULL, 0, NULL, 0},
    };
    char *short_options = long_options_to_short_options(long_options);

    for (;;) { //process all the long options
        int c;

        /*gets the corresponding short options.*/
        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage();

        case 'V':
            ovs_print_version(OFP10_VERSION, OFP10_VERSION);
            exit(EXIT_SUCCESS);

        case OPT_MLOCKALL: //lock all the memory into physical RAM
            want_mlockall = true;
            break;

        case OPT_UNIXCTL:
            *unixctl_pathp = optarg; //the unixctl option, then override default ctl path
            break;

        VLOG_OPTION_HANDLERS //log
        DAEMON_OPTION_HANDLERS //detach, pid, chdir, etc.
        LEAK_CHECKER_OPTION_HANDLERS
        STREAM_SSL_OPTION_HANDLERS

        case OPT_PEER_CA_CERT:
            stream_ssl_set_peer_ca_cert_file(optarg);
            break;

        case OPT_BOOTSTRAP_CA_CERT:
            stream_ssl_set_ca_cert_file(optarg, true);
            break;

        case OPT_ENABLE_DUMMY:
            dummy_enable(optarg && !strcmp(optarg, "override"));
            break;

        case OPT_DISABLE_SYSTEM:
            dp_blacklist_provider("system");
            break;

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }
    free(short_options);

    argc -= optind;
    argv += optind;

    /*get the ovsdb socket.*/
    switch (argc) {
    case 0:
        return xasprintf("unix:%s/db.sock", ovs_rundir());

    case 1:
        return xstrdup(argv[0]);

    default:
        VLOG_FATAL("at most one non-option argument accepted; "
                   "use --help for usage");
    }
}

static void
usage(void)
{
    printf("%s: Open vSwitch daemon\n"
           "usage: %s [OPTIONS] [DATABASE]\n"
           "where DATABASE is a socket on which ovsdb-server is listening\n"
           "      (default: \"unix:%s/db.sock\").\n",
           program_name, program_name, ovs_rundir());
    stream_usage("DATABASE", true, false, true);
    daemon_usage();
    vlog_usage();
    printf("\nOther options:\n"
           "  --unixctl=SOCKET        override default control socket name\n"
           "  -h, --help              display this help message\n"
           "  -V, --version           display version information\n");
    leak_checker_usage();
    exit(EXIT_SUCCESS);
}

static void
ovs_vswitchd_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                  const char *argv[] OVS_UNUSED, void *exiting_)
{
    bool *exiting = exiting_;
    *exiting = true;
    unixctl_command_reply(conn, NULL);
}
