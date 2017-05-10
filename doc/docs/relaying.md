Relaying is the process by which one server mirrors one or more streams from a remote server. The servers
need not be of the same type (i.e. Icecast can relay from Shoutcast). Relaying is used primarily for large
broadcasts that need to distribute listening clients across multiple physical machines.


# Type of Relays
There are two types of relays that Icecast supports:  
The first type is when both master and slave servers are Icecast 2 servers. In this case, a “master-slave” relay
can be setup such that all that needs to be done is configure the slave server with the connection information
(server IP and port) of the master server and the slave will mirror all mountpoints on the master server. The slave
will also periodically check the master server to see if any new mountpoints have attached and if so will relay those
as well.

The second type of relay is a specific mountpoint relay. In this case, the slave server is configured with a
server IP, port and mountpoint and only the mountpoint specified is relayed.


# Setting Up a Master-Slave Relay
In order to setup a relay of this type both servers (the one you wish to relay and the one doing the relaying)
need to be Icecast 2 servers.  
The following configuration snippet is used as an example:

```xml
<master-server>192.168.1.11</master-server>
<master-server-port>8001</master-server-port>
<master-update-interval>120</master-update-interval>
<master-username>relay</master-username>
<master-password>hackme</master-password>
<relays-on-demand>0</relays-on-demand>
```

In this example, this configuration is setup in the server which will be doing the relaying (slave server).
The master server in this case need not be configured (and actually is unaware of the relaying being performed).
When the slave server is started, it will connect to the master server, 192.168.1.11:8001 in this example. The slave server will begin to relay all non-hidden mountpoints connected to the master server. Additionally, every master-update-interval, 120 seconds
in this case, the slave server will poll the master server to see if any new mountpoints have connected.  
Note that the names of the mountpoints on the slave server will be identical to those on the master server.

Configuration options:

master-server
: This is the hostname (or IP) for the server which contains the mountpoints to be relayed (Master Server).

master-server-port
: This is the TCP port for the server which contains the mountpoints to be relayed (Master Server).

master-update-interval
: The interval in seconds that the relay server will poll the master server for any new mountpoints to relay.

master-username
: This is the relay username for the master server, used to query the server for a list of mountpoints to relay.  
  (Defaults to `relay`)

master-password
: This is the relay password for the master server, used to query the server for a list of mounpoints to relay.

relays-on-demand
: Global on-demand setting for relays. Because you do not have individual relay options when using a master server relay, you still may want those relays to only pull the stream when there is at least one listener on the slave. The typical case here is to avoid bandwidth costs when no one is listening.

# Specific Mountpoint Relay
If only specific mountpoints need to be relayed, or the master server is not a Icecast 2 server, you can use the specific
mountpoint relay. Supported master servers for this type of relay are Shoutcast, Icecast 1.x, and of course Icecast 2.  
The following configuration snippet is used as an example:

```xml
<relay>
    <server>192.168.1.11</server>
    <port>8001</port>
    <mount>/example.ogg</mount>
    <local-mount>/different.ogg</local-mount>
    <username>Jekyll</username>
    <password>Hyde</password>
    <relay-shoutcast-metadata>0</relay-shoutcast-metadata>
    <on-demand>1</on-demand>
</relay>
```

In this example, this configuration is setup in the server which will be doing the relaying (slave server). 
The master server in this case need not be configured (and actually is unaware of the relaying being performed) as a
relay. When the slave server is started, it will connect to the master server, in this example located at 192.168.1.11:8001
and will begin to relay only the mountpoint specified (/example.ogg in this case).  
Using this type of relay, the user can override the local mountpoint name and make it something entirely different than the one on the master server. Additionally, if the server is a Shoutcast server, then the `<mount>` must be specified as `/`,
and if you want the Shoutcast relay stream to have metadata contained within it (Shoutcast metadata is embedded
in the stream itself), the `<relay-shoutcast-metadata>` needs to be set to `1`.

Configuration options:

server
: This is the hostname (or IP) for the server which contains the mountpoint to be relayed.

port
: This is the TCP port for the server which contains the mountpoint to be relayed.

mount
: The mountpoint located on the remote server. (If you are relaying a Shoutcast stream, this should be `/`)

local-mount
: The name to use for the local mountpoint. This is what the mountpoint will be called on the relaying server. (Defaults to the remote mountpoint)

username
: The username, if required, for the remote mountpoint.

password
: The password, if required, for the remote mountpoint.

relay-shoutcast-metadata
: If relaying a Shoutcast stream, set this to `1` to relay the metadata (song titles), which are part of the Shoutcast data stream. (Defaults to enabled, but it's up to the remote server if metadata is sent)  
  Possible values: `1`: enabled, `0`: disabled

on-demand
: An on-demand relay will only retrieve the stream if there are listeners requesting the stream. (Defaults to  the value of `<relays-on-demand>`)  
  Possible values: `1`: enabled, `0`: disabled