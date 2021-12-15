from pyroute2 import netns, NDB, netlink, NSPopen
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
COMMAND_TIMEOUT = 60


@contextmanager
def managed_nspopen(*args, **kwds):
    proc = NSPopen(*args, **kwds)
    try:
        yield proc
    finally:
        if proc.poll() is None:
            # send SIGKILL to the process and wait for it to die if it's still
            # running
            proc.kill()
            # If it's not dead after 2 seconds we throw an error
            proc.communicate(timeout=2)

        # release proxy process resourecs
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
        self.bridgeName = "%s-br" % (namePrefix,)
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

        with NDB() as ndb:
            (
                ndb.interfaces.create(ifname=name, kind="bridge", state="up")
                .add_ip("%s/%s" % (address, prefixLen))
                .commit()
            )

    def _add_interface_to_bridge(self, bridge, interface):
        """
        Adds the given interface to the bridge. In our usecase, this interface
        is usually the peer end of a veth pair with the other end inside a
        network namespace, in which case after calling this function the namespace
        will be able to communicate with the other nodes in the virtual network.
        """
        with NDB() as ndb:
            ndb.interfaces[bridge].add_port(interface).commit()
            ndb.interfaces[interface].set(state="up").commit()


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
        sudo_command = [
            "sudo",
            "-E",
            "-u",
            user,
            "env",
            "PATH=" + os.getenv("PATH"),
        ] + command
        return managed_nspopen(
            self.namespace,
            sudo_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            start_new_session=True,
        )

    def run_unmanaged(self, command, user=os.getenv("USER")):
        """
        Executes a command under the given user from this virtual node. Returns
        an NSPopen object to control the process. NSOpen has the same API as
        subprocess.Popen. This NSPopen object needs to be manually release. In
        general you should prefer using run, where this is done automatically
        by the context manager.
        """
        sudo_command = [
            "sudo",
            "-E",
            "-u",
            user,
            "env",
            "PATH=" + os.getenv("PATH"),
        ] + command
        return NSPopen(
            self.namespace,
            sudo_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            start_new_session=True,
        )

    def run_and_wait(self, command, name, timeout=COMMAND_TIMEOUT):
        """
        Waits for command to exit successfully. If it exits with error or it timeouts,
        raises an exception with stdout and stderr streams of the process.
        """
        with self.run(command) as proc:
            try:
                out, err = proc.communicate(timeout=timeout)
                if proc.returncode > 0:
                    raise Exception(
                        "%s failed, out: %s\n, err: %s" % (name, out, err)
                    )
                return out, err
            except subprocess.TimeoutExpired:
                proc.kill()
                out, err = proc.communicate()
                raise Exception(
                    "%s timed out after %d seconds. out: %s\n, err: %s"
                    % (name, timeout, out, err)
                )

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

        with NDB() as ndb:
            #
            # Add netns to the NDB sources
            #
            # ndb.interfaces["lo"] is a short form of
            # ndb.interfaces[{"target": "localhost", "ifname": "lo"}]
            #
            # To address interfaces/addresses/routes wthin a netns, use
            # ndb.interfaces[{"target": netns_name, "ifname": "lo"}]
            ndb.sources.add(netns=name)
            #
            # Create veth
            (
                ndb.interfaces.create(
                    ifname=veth_name,
                    kind="veth",
                    peer=self.vethPeer,
                    state="up",
                )
                .commit()
                .set(net_ns_fd=name)
                .commit()
            )
            #
            # .interfaces.wait() returns an interface object when
            # it becomes available on the specified source
            (
                ndb.interfaces.wait(target=name, ifname=veth_name)
                .set(state="up")
                .add_ip("%s/%s" % (address, netmaskLength))
                .commit()
            )
            #
            (
                ndb.interfaces[{"target": name, "ifname": "lo"}]
                .set(state="up")
                .commit()
            )

    def _remove_namespace_if_exists(self, name):
        """
        If the given namespace exists, removes it. Otherwise just returns
        silently.
        """
        try:
            netns.remove(name)
        except Exception:
            # Namespace doesn't exist. Return silently.
            pass

    def ifdown(self):
        """
        Bring the network interface down for this node
        """
        with NDB() as ndb:
            # bring it down and wait until success
            ndb.interfaces[self.vethPeer].set(state="down").commit()

    def ifup(self):
        """
        Bring the network interface up for this node
        """
        with NDB() as ndb:
            # bring it up and wait until success
            ndb.interfaces[self.vethPeer].set(state="up").commit()


def _remove_interface_if_exists(name):
    """
    If the given interface exists, brings it down and removes it. Otherwise
    just returns silently. A bridge is also an interface, so this can be
    used for removing bridges too.
    """
    with NDB() as ndb:
        if name in ndb.interfaces:
            try:
                ndb.interfaces[name].remove().commit()
            except netlink.exceptions.NetlinkError:
                pass
