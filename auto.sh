#This is a temporal script to test quick operations, e.g., generating docs.

<<<<<<< HEAD
#doxygen doxygen.cfg; tar czf doc.tar.gz doc/

#cscope
#find . -name "*.h" -o -name "*.c" -o -name "*.cc" -o -name "*.inc" | cscope -bkq

=======
>>>>>>> ovs-master
#Configure openvswitch
./boot.sh && ./configure  --with-linux=/lib/modules/`uname -r`/build;

#Compile and install openvswitch
<<<<<<< HEAD
make || print "make failed" && exit
sudo su;
make install;

rmmod bridge >/dev/null
rmmod openvswitch >/dev/null 2>&1
insmod datapath/linux/openvswitch.ko || print "insmod failed, check dmesg" && exit

#configure the ovs-db
test -d /usr/local/etc/openvswitch || mkdir -p /usr/local/etc/openvswitch
test -f /usr/local/etc/openvswitch/conf.db || ovsdb-tool create /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema
=======
make && sudo make install;

#insmod datapath/linux/openvswitch.ko
modprobe datapath/linux/openvswitch.ko

#configure the ovs-db
mkdir -p /usr/local/etc/openvswitch
ovsdb-tool create /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema
>>>>>>> ovs-master

#start the ovs-db server
ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
             --remote=db:Open_vSwitch,manager_options \
             --private-key=db:SSL,private_key \
             --certificate=db:SSL,certificate \
             --bootstrap-ca-cert=db:SSL,ca_cert \
             --pidfile --detach

<<<<<<< HEAD
#Initialize the ovs-db, just pass if initialized already
=======
#Initialize the ovs-db
>>>>>>> ovs-master
ovs-vsctl --no-wait init

#start the main ovs daemon
ovs-vswitchd --pidfile --detach

#create a new vs
ovs-vsctl add-br br0
<<<<<<< HEAD
ovs-vsctl add-br br1
ovs-vsctl add-port br0 br0-br1 -- set Interface br0-br1 type=patch options:peer=br1-br0
ovs-vsctl add-port br1 br1-br0 -- set Interface br1-br0 type=patch options:peer=br0-br1
ovs-vsctl show

#configure controller on localhost:6633
ovs-vsctl set-controller br0 tcp:127.0.0.1
ovs-vsctl set-controller br1 tcp:127.0.0.1

#stop the ovs daemon
kill `cd /usr/local/var/run/openvswitch && cat ovsdb-server.pid ovs-vswitchd.pid`

#upgrade the database by new .ovsschema file
ovsdb-tool convert /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema
cp /usr/local/etc/openvswitch/conf.db ./ovsdb/conf.db.bak
=======

#stop the ovs daemon
kill `cd /usr/local/var/run/openvswitch && cat ovsdb-server.pid ovs-vswitchd.pid`
>>>>>>> ovs-master
