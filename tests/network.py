from pyroute2 import netns, IPDB, IPRoute, netlink, NetNS, NSPopen
from contextlib import contextmanager
import ipaddress
import subprocess
import os
import os.path

"""
TODO: Add an introduction to network namespaces, veth interfaces, and bridges,
and explain why we use them here.
"""

BRIDGE_NF_CALL_IPTABLES = "/proc/sys/net/bridge/bridge-nf-call-iptables"


@contextmanager
def managed_nspopen(*args, **kwds):
    # Code to acquire resource, e.g.:
    proc = NSPopen(*args, **kwds)
    try:
        yield proc
    finally:
        # Code to release resource, e.g.:
        proc.release()


class VirtualLAN:
    """
    Helper class to create a network of virtual nodes to simulate a virtual network.
    IP addresses are assigned automatically to the nodes from a private IP range.
    IP address of a virtual node can be accessed using the node.address field.

    Internally, this is a network of Linux network namespaces connected by a
    bridge.
    TODO: explain more details and add an example.
    """
    def __init__(self, namePrefix, subnet):
        ipnet = ipaddress.ip_network(subnet)
        self.availableHosts = ipnet.hosts()
        self.prefixLen = ipnet.prefixlen
        self.namePrefix = namePrefix
        self.nodes = []
        # create the bridge
        self.bridgeName = ("%s-br" % (namePrefix, ))
        self.bridgeAddress = next(self.availableHosts)
        self._add_bridge(self.bridgeName, self.bridgeAddress, self.prefixLen)
        # Don't pass bridged IPv4 traffic to iptables' chains, so namespaces
        # can communicate irrespective of the host machines iptables. This is
        # needed in some docker instances (e.g. travis), where traffic was
        # filtered at bridge level. See
        # https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt
        try:
            with open(BRIDGE_NF_CALL_IPTABLES, "r") as f:
                self.saved_bridge_nf_call_iptables = f.read()
            with open(BRIDGE_NF_CALL_IPTABLES, "w") as f:
                f.write("0\n")
        except FileNotFoundError:
            # In some environments this variable doesn't exist, we are ok with
            # no changes in this case.
            self.saved_bridge_nf_call_iptables = None

    def create_node(self):
        """
        Creates a VirtualNode which can access/be accessed from other nodes in
        the virtual network.
        """
        namespace = "%s-%s" % (self.namePrefix, len(self.nodes))
        address = next(self.availableHosts)
        node = VirtualNode(namespace, address, self.prefixLen)
        self._add_interface_to_bridge(self.bridgeName, node.vethPeer)
        self.nodes.append(node)
        return node

    def destroy(self):
        """
        Destroys the objects created for the virtual network.
        """
        for node in self.nodes:
            node.destroy()
        _remove_interface_if_exists(self.bridgeName)
        if self.saved_bridge_nf_call_iptables is not None:
            with open(BRIDGE_NF_CALL_IPTABLES, "w") as f:
                f.write(self.saved_bridge_nf_call_iptables)

    def _add_bridge(self, name, address, prefixLen):
        """
        Creates a bridge with the given name, address, and netmask perfix length.
        """
        _remove_interface_if_exists(name)

        with IPRoute() as ipr:
            ipr.link('add', ifname=name, kind='bridge')

        with IPDB() as ipdb:
            with ipdb.interfaces[name] as bridge:
                bridge.add_ip('%s/%d' % (address, prefixLen, ))
                bridge.up()

    def _add_interface_to_bridge(self, bridge, interface):
        """
        Adds the given interface to the bridge. In our usecase, this interface
        is usually the peer end of a veth pair with the other end inside a
        network namespace, in which case after calling this function the namespace
        will be able to communicate with the other nodes in the virtual network.
        """
        with IPRoute() as ipr:
            bridge_idx = ipr.link_lookup(ifname=bridge)[0]
            interface_idx = ipr.link_lookup(ifname=interface)[0]
            ipr.link('set', index=interface_idx, master=bridge_idx)
            ipr.link('set', index=interface_idx, state='up')

class VirtualNode:
    """
    A virtual node inside a virtual network.

    Internally, this corresponds to a Linux network namespace.
    """
    def __init__(self, namespace, address, prefixLen):
        self.namespace = namespace
        self.address = address
        self.prefixLen = prefixLen
        self.vethPeer = namespace + "p"
        self._add_namespace(namespace, address, prefixLen)

    def destroy(self):
        """
        Removes all objects created for the virtual node.
        """
        _remove_interface_if_exists(self.vethPeer)
        try:
            netns.remove(self.namespace)
        except:
            # Namespace doesn't exist. Return silently.
            pass

    def run(self, command, user=os.getenv("USER")):
        """
        Executes a command under the given user from this virtual node. Returns
        a context manager that returns NSOpen object to control the process.
        NSOpen has the same API as subprocess.POpen.
        """
        sudo_command = ['sudo', '-E', '-u', user,
                        'env', 'PATH=' + os.getenv("PATH")] + command
        return managed_nspopen(self.namespace, sudo_command,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, universal_newlines=True,
                               start_new_session=True)

    def run_unmanaged(self, command, user=os.getenv("USER")):
        """
        Executes a command under the given user from this virtual node. Returns
        an NSPopen object to control the process. NSOpen has the same API as
        subprocess.Popen. This NSPopen object needs to be manually release. In
        general you should prefer using run, where this is done automatically
        by the context manager.
        """
        sudo_command = ['sudo', '-E', '-u', user,
                        'env', 'PATH=' + os.getenv("PATH")] + command
        return NSPopen(self.namespace, sudo_command,
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, universal_newlines=True,
                       start_new_session=True)


    def _add_namespace(self, name, address, netmaskLength):
        """
        Creates a namespace with the given name, and creates a veth interface
        with one endpoint inside the namespace which has the given address and
        netmask length. The peer end of veth interface can be used to connect the
        namespace to a bridge.
        """

        self._remove_namespace_if_exists(name)
        netns.create(name)

        veth_name = "veth0"

        _remove_interface_if_exists(self.vethPeer)

        # Create the veth pair and set one endpoint to the namespace.
        with IPRoute() as ipr:
            ipr.link('add', ifname=veth_name, kind='veth', peer=self.vethPeer)
            idx = ipr.link_lookup(ifname=veth_name)[0]
            ipr.link('set', index=idx, net_ns_fd=name)

        # Assign address to the veth interface and bring it up.
        with IPDB(nl=NetNS(name)) as ipdb:
            with ipdb.interfaces[veth_name] as veth:
                veth.add_ip('%s/%d' % (address, netmaskLength, ))
                veth.up()

            # Bring the loopback interface up.
            with ipdb.interfaces['lo'] as lo:
                lo.up()

    def _remove_namespace_if_exists(self, name):
        """
        If the given namespace exists, removes it. Otherwise just returns
        silently.
        """
        try:
            netns.remove(name)
        except:
            # Namespace doesn't exist. Return silently.
            pass

def _remove_interface_if_exists(name):
    """
    If the given interface exists, brings it down and removes it. Otherwise
    just returns silently. A bridge is also an interface, so this can be
    used for removing bridges too.
    """
    with IPRoute() as ipr:
        # bring it down
        try:
            ipr.link('set', ifname=name, state='down')
        except netlink.exceptions.NetlinkError:
            pass
        # remove it
        try:
            ipr.link('del', ifname=name)
        except netlink.exceptions.NetlinkError:
            pass
