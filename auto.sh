#This is a temporal script to test quick operations, e.g., generating docs.

#Configure openvswitch
./boot.sh && ./configure  --with-linux=/lib/modules/`uname -r`/build;

#Compile and install openvswitch
make && sudo make install;

#insmod datapath/linux/openvswitch.ko
modprobe datapath/linux/openvswitch.ko

#configure the ovs-db
mkdir -p /usr/local/etc/openvswitch
ovsdb-tool create /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema

#start the ovs-db server
ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
             --remote=db:Open_vSwitch,manager_options \
             --private-key=db:SSL,private_key \
             --certificate=db:SSL,certificate \
             --bootstrap-ca-cert=db:SSL,ca_cert \
             --pidfile --detach

#Initialize the ovs-db
ovs-vsctl --no-wait init

#start the main ovs daemon
ovs-vswitchd --pidfile --detach

#create a new vs
ovs-vsctl add-br br0

#stop the ovs daemon
kill `cd /usr/local/var/run/openvswitch && cat ovsdb-server.pid ovs-vswitchd.pid`
