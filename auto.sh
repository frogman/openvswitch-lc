#This is a temporal script to test quick operations, e.g., generating docs.

#doxygen doxygen.cfg; tar czf doc.tar.gz doc/

#cscope
#find . -name "*.h" -o -name "*.c" -o -name "*.cc" -o -name "*.inc" | cscope -bkq

#Configure openvswitch
./boot.sh && ./configure  --with-linux=/lib/modules/`uname -r`/build;

#Compile and install openvswitch
make || print "make failed" && exit
sudo su;
make install;

rmmod bridge >/dev/null
rmmod openvswitch >/dev/null 2>&1
insmod datapath/linux/openvswitch.ko || print "insmod failed, check dmesg" && exit

#configure the ovs-db
test -d /usr/local/etc/openvswitch || mkdir -p /usr/local/etc/openvswitch
test -f /usr/local/etc/openvswitch/conf.db || ovsdb-tool create /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema

#start the ovs-db server
ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
             --remote=db:Open_vSwitch,manager_options \
             --private-key=db:SSL,private_key \
             --certificate=db:SSL,certificate \
             --bootstrap-ca-cert=db:SSL,ca_cert \
             --pidfile --detach

#Initialize the ovs-db, just pass if initialized already
ovs-vsctl --no-wait init

#start the main ovs daemon
ovs-vswitchd --pidfile --detach

#create a new vs
ovs-vsctl add-br br0
ovs-vsctl list-br

#configure controller on localhost:6633
ovs-vsctl set-controller br0 tcp:127.0.0.1

#stop the ovs daemon
kill `cd /usr/local/var/run/openvswitch && cat ovsdb-server.pid ovs-vswitchd.pid`

#backup the database to .ovsschema file
ovsdb-tool convert /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema
rm /usr/local/etc/openvswitch/conf.db
