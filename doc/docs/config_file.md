This section will describe each section of the configuration file.

!!! note
    Especially for new Icecast users, editing the config file can be quite tricky.
    It is thus recommended to make a __backup of the original config file__ and then
    __start by just changing all passwords__, nothing else.

Should you need to customize the configuration, then make a backup of your working config file, before you
make any changes. If Icecast refuses to start it is in most cases due to a malformed config file. In such a
case running the following command should point out most XML syntax problems:

    xmllint icecast.xml

Also check the Icecast error.log for additional hints in case of problems!

# General Settings

```xml
<hostname>example.org</hostname>
<location>Moon</location>
<admin>icemaster@example.org</admin>
<fileserve>1</fileserve>
<server-id>icecast 2.4.1</server-id>
```

hostname
: This is the DNS name (or IP address) that will be used for the stream directory lookups or
  possibily the playlist generation if a Host header is not provided. This should be something
  that your listeners can use.  
  __Note__: This should not the the URL of your stream's website or so, but the hostname for this
  Icecast server!

location
: This sets the location string for this Icecast instance. It will be shown e.g on the web interface.

admin
: This should contain contact details for getting in touch with the server administrator.
  Usually this will be an email address, but as this can be an arbitrary string it could also
  be a phone number. This will be shown e.g. on the web interface.

fileserve
: This flag turns on the Icecast fileserver from which static files can be served. All files
  are served relative to the path specified in the [`<webroot>`](#path-settings) configuration setting.  
  By default the setting is enabled so that requests for the static files needed by the status 
  and admin pages, such as images and CSS are retrievable.

server-id
: This optional setting allows for the administrator of the server to override the default
  server identification. The default is icecast followed by a version number.  
  It is not recommended to use this setting, unless you have very good reasons and know
  what you are doing.

# Limits

```xml
<limits>
    <clients>100</clients>
    <sources>2</sources>
    <queue-size>102400</queue-size>
    <client-timeout>30</client-timeout>
    <header-timeout>15</header-timeout>
    <source-timeout>10</source-timeout>
    <burst-on-connect>1</burst-on-connect>
    <burst-size>65536</burst-size>
</limits>
```

This section contains server level settings. Usually only the `<clients>` and `<sources>` values
need to be adjusted.  
Only modify this section if you know what you are doing.

clients
: Total number of concurrent clients supported by the server. Listeners are considered clients,
  but so are accesses to any static content (i.e. fileserved content) and also any requests to
  gather stats. These are max concurrent connections for the entire server (not per mountpoint).

sources
: Maximum number of connected sources supported by the server. This includes active relays and source clients.

queue-size
: This is the maximum size (in bytes) of the stream queue. A listener may temporarily
  lag behind due to network congestion and in this case an internal queue is maintained for the
  listeners. If the queue grows larger than this config value, then it is truncated and any listeners
  found will be removed from the stream. This will be the default setting for the streams which is
  512k unless overridden here.  
  You can override this in the individual mount settings as well, which can be
  useful if you have a mixture of high bandwidth video and low bitrate audio streams.

client-timeout
: This does not seem to be used.

header-timeout
: The maximum time (in seconds) to wait for a request to come in once the client has made a connection
  to the server. In general this value should not need to be tweaked.

source-timeout
: If a connected source does not send any data within this timeout period (in seconds),
  then the source connection will be removed from the server.

burst-on-connect
: This option is deprecated, use `burst-size` instead.

burst-size
: The burst size is the amount of data (in bytes) to burst to a client at connection time. This is to quickly fill
  the pre-buffer used by media players. The default is 64 kbytes which is a typical size used by most clients so changing
  it is usually not required. This setting applies to all mountpoints unless overridden in the mount settings. Ensure that this value is smaller than queue-size, if necessary increase queue-size to be larger than your desired burst-size. Failure to do so might result in aborted listener client connection attempts, due to initial burst leading to the connection already exceeding the queue-size limit.

# Authentication
This section contains all the usernames and passwords used for administration purposes or to connect sources and relays.
For more information, refer to the [Authentication](auth.md) Page.

# Public Directory Publishing Settings

```xml
<directory>
    <yp-url-timeout>15</yp-url-timeout>
    <yp-url>http://dir.xiph.org/cgi-bin/yp-cgi</yp-url>
</directory>
```

This section contains all the settings for listing a stream on any of the Icecast Directory servers.
Multiple occurances of this section can be specified in order to be listed on multiple directory servers.  
For more Information see the [Listing in a Directory](yp.md) Page.

yp-url-timeout
: This value is the maximum time Icecast will wait for a response from a particular directory server.
  The recommended value should be sufficient for most directory servers.

yp-url
: The URL which Icecast uses to communicate with the Directory server.
  The value for this setting is provided by the owner of the Directory server.


# TCP Port settings

The following shows how you can specify the listening settings for the server.

```xml
<listen-socket>
    <port>8000</port>
    <bind-address>127.0.0.1</bind-address>
</listen-socket>

<listen-socket>
    <port>8443</port>
    <tls>1</tls>
</listen-socket>

<listen-socket>
    <port>8004</port>
    <shoutcast-mount>/live.mp3</shoutcast-mount>
</listen-socket>
```

The first listen-socket block shows how to make Icecast listen on port 8000, and additionally specifies
a `<bind-address>`, which limits this port to only listen for connections from this address.  
If a bind-address is not specified for a particular listen-socket, then the socket will be bound to all
interfaces (including IPv6 if available). For most people, the bind-address option will not
be required and often confuses people.

Another possibility is to use an `<ssl>` boolean setting which informs Icecast that a secured
connection is to be used. A common use for using a secure connection would be for admin page access.

The last listen-socket block in this example shows how to defined a Shoutcast compatible port. This can
be done by setting the `shoutcast-mount` setting. This will implicity define a second listening socket
whose port number is always one higher than the port defined (because the Shoutcast protocol requires
two ports) and also informs Icecast of which mountpoint the Shoutcast source client on this port will be using.

port
: The TCP port that will be used to accept client connections.

bind-address
: An optional IP address that can be used to bind to a specific network
  card. If not supplied, then it will bind to all interfaces.

tls
: If set to 1 will enable HTTPS on this listen-socket. Icecast must have been compiled against OpenSSL to be able to do so.

shoutcast-mount
: An optional mountpoint setting to be used when Shoutcast DSP compatible clients connect.  
  Defining this within a listen-socket group tells Icecast that this port and the subsequent port are to be used for
  Shoutcast compatible source clients.

# HTTP headers

```xml
<http-headers>
    <header name="Access-Control-Allow-Origin" value="*" />
    <header name="X-Robots-Tag" value="index, noarchive" status="200" />
</http-headers>
```

Icecast can be configured to send custom HTTP headers. This is available as a global setting and inside mountpoints. This section explains the global settings.

This functionality was introduced mainly to enable the use of simplified cross-origin resource sharing. The Icecast default configuration contains the first header, as seen in the above exmple, for this reason.

<dl>
    <dt>http-headers</dt>
    <dd>This element is placed anywhere inside the main section of the Icecast config.
        It will contain <code>&lt;header&gt;</code> child elements, that specify the actual headers one by one.</dd>

    <dt>header</dt>
    <dd>This tag specifies the actual header to be sent to a HTTP client in response to every request.<br />
        This tag can contain the following attributes:
        <dl>
            <dt>name</dt>
            <dd>Specifies the HTTP header field name. (required)</dd>
            <dt>value</dt>
            <dd>Specifies the HTTP header field value. (required)</dd>
            <dt>status</dt>
            <dd>Limits sending the header to certain HTTP status codes.<br />
                If not specified, the default is to return the header for every HTTP status code.
                This attribute is only available for global headers, at the moment. (optional)
            </dd>
        </dl>
    </dd>
</dl>

At the moment only global headers will be sent in case the HTTP status is not "200". This is subject to change in the future.
Avoid placing comments inside `<http-headers>` as, in this release, it will prevent Icecast from parsing further `<header>` tags.

# Relaying Streams

This section contains the servers relay settings. The relays are implemented using a pull system where the receiving
server connects as if it's a listener to the sending server.  
There are two types of relay setups, a “Master server relay” or a “Specific Mountpoint relay.”

For information about the two types and how to configure them, refer to the [Relaying](relaying.md) Page.

# Mount Specific Settings

<!-- FIXME -->

```xml
<mount type="normal">
    <mount-name>/example-complex.ogg</mount-name>
    <username>othersource</username>
    <password>hackmemore</password>
    <max-listeners>1</max-listeners>
    <max-listener-duration>3600</max-listener-duration>
    <dump-file>/tmp/dump-example1.ogg</dump-file>
    <intro>/intro.ogg</intro>
    <fallback-mount>/example2.ogg</fallback-mount>
    <fallback-override>1</fallback-override>
    <fallback-when-full>1</fallback-when-full>
    <charset>ISO-8859-1</charset>
    <public>1</public>
    <stream-name>My audio stream</stream-name>
    <stream-description>My audio description</stream-description>
    <stream-url>http://some.place.com</stream-url>
    <genre>classical</genre>
    <bitrate>64</bitrate>
    <type>application/ogg</type>
    <subtype>vorbis</subtype>
    <hidden>1</hidden>
    <burst-size>65536</burst-size>
    <mp3-metadata-interval>4096</mp3-metadata-interval>
    <authentication type="xxxxxx">
            <!-- See authentication documentation -->
    </authentication>
    <http-headers>
            <header name="Access-Control-Allow-Origin" value="*" />
            <header name="X-Robots-Tag" value="index, noarchive" />
            <header name="foo" value="bar" status="200" />
            <header name="Nelson" value="Ha-Ha!" status="404" />
    </http-headers>
    <on-connect>/home/icecast/bin/source-start</on-connect>
    <on-disconnect>/home/icecast/bin/source-end</on-disconnect>
</mount>
```

This section contains the settings which apply only to a specific mountpoint and applies to an incoming
stream whether it is a relay or a source client. The purpose of the mount definition is to state certain
information that can override either global/default settings or settings provided from the incoming stream.

A mount does not need to be stated for each incoming source although you may want to specific certain settings
like the maximum number of listeners or a mountpoint specific username/password. As a general rule, only define
what you need to but each mount definition needs at least the mount-name. Changes to most of these will apply
across a configuration file re-read even on active streams, however some only apply when the stream starts or
ends.

type
: The type of the mount point (default: "normal"). A mount of type "default"
  can be used to specify common values for multiple mountpoints.  
  Note that default mountpoints won't merge with other mount blocks.
  You only get those values if no `type="normal"` mount block exists
  corresponding to your mountpoint.

mount-name
: The name of the mount point for which these settings apply.
  MUST NOT be used in case of mount type "default".

<!-- FIXME -->
username
: An optional value which will set the username that a source must use to connect using this mountpoint.
  Do not set this value unless you are sure that the source clients connecting to the mount point can be
  configured to send a username other than `source`.  
  If this value is not present the default username is `source`.

<!-- FIXME -->
password
: An optional value which will set the password that a source must use to connect using this mountpoint.
  There is also a [URL based authentication method](auth.html#stream-auth) for sources that can be used instead.

max-listeners
: An optional value which will set the maximum number of listeners that can be attached to this mountpoint.

max-listener-duration
: An optional value which will set the length of time a listener will stay connected to the stream.  
  An auth component may override this.

dump-file
: An optional value which will set the filename which will be a dump of the stream coming through 
  on this mountpoint. This filename is processed with strftime(3). This allows to use variables like `%F`.

intro
: An optional value which will specify the file those contents will be sent to new listeners when they
  connect but before the normal stream is sent. Make sure the format of the file specified matches the
  streaming format. The specified file is appended to webroot before being opened.

fallback-mount
: This optional value specifies a mountpoint that clients are automatically moved
  to if the source shuts down or is not streaming at the time a listener connects. Only one can be
  listed in each mount and should refer to another mountpoint on the same server that is streaming in
  the same streaming format.  
  If clients cannot fallback to another mountpoint, due to a missing
  fallback-mount or it states a mountpoint that is just not available, then those clients will be
  disconnected. If clients are falling back to a mountpoint and the fallback-mount is not actively
  streaming but defines a fallback-mount itself then those clients may be moved there instead. This
  multi-level fallback allows clients to cascade several mountpoints.  
  A fallback mount can also state a file that is located in webroot. This is useful for playing a
  pre-recorded file in the case of a stream going down. It will repeat until either the listener
  disconnects or a stream comes back available and takes the listeners back. As per usual, the file
  format should match the stream format, failing to do so may cause problems with playback.  
  Note that the fallback file is not timed so be careful if you intend to relay this. They are fine
  on slave streams but don't use them on master streams, if you do then the relay will consume stream
  data at a faster rate and the listeners on the relay would eventually get kicked off.

fallback-override
: When enabled, this allows a connecting source client or relay on this mountpoint to move listening
  clients back from the fallback mount.

fallback-when-full
: When set to `1`, this will cause new listeners, when the max listener count for the mountpoint has
  been reached, to move to the fallback mount if there is one specified.

charset
: For legacy, non-Ogg streams like MP3, the metadata that is inserted into the stream often has no defined character set.
  We have traditionally assumed UTF8 as it allows for multiple language sets on the web pages and stream directory,
  however many source clients for MP3 type streams have assumed Latin1 (ISO-8859-1) or leave it to whatever character
  set is in use on the source client system.  
  This character mismatch has been known to cause a problem as the stats engine and stream directory servers want UTF8
  so now we assume Latin1 for non-Ogg streams (to handle the common case) but you can specify an alternative character
  set with this option.  
  The source clients can also specify a `charset=` parameter to the metadata update URL if they so wish.

public
: The default setting for this is `-1` indicating that it is up to the source client or relay to determine if this mountpoint
  should advertise. A setting of `0` will prevent any advertising and a setting of `1` will force it to advertise. 
  If you do force advertising you may need to set other settings listed below as the directory server can refuse to advertise
  if there is not enough information provided.

stream-name
: Setting this will add the specified name to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.

stream-description
: Setting this will add the specified description to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.

stream-url
: Setting this will add the specified URL to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.
  The URL is generally for directing people to a website.

genre
: Setting this will add the specified genre to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.
  This can be anything be using certain key words can help searches in the directories.

bitrate
: Setting this will add the specified bitrate to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.
  This is stated in kbps.

type
: Setting this will add the specified mime type to the stats (and therefore directory listings) for this mountpoint even if the source client/relay provide one.
  It is very unlikely that this will be needed.

subtype
: Setting this will add the specified subtype to the stats (and therefore directory listings) for this mountpoint.
  The subtype is really to help the directory server to identify the components of the type.
  An example setting is vorbis/theora and indicates the codecs in an Ogg stream

burst-size
: This optional setting allows for providing a burst size which overrides the default burst size as defined in limits.
  The value is in bytes.

mp3-metadata-interval
: This optional setting specifies what interval, in bytes, there is between metadata updates within shoutcast compatible streams.
  This only applies to new listeners connecting on this mountpoint, not existing listeners falling back to this mountpoint. The
  default is either the hardcoded server default or the value passed from a relay.

hidden
: Enable this to prevent this mount from being shown on the xsl pages. This is mainly for cases where a local relay is configured
  and you do not want the source of the local relay to be shown.

<!-- FIXME -->
authentication
: This specifies that the named mount point will require listener (or source) authentication. Currently, we support a file-based
  authentication scheme (`type=htpasswd`) and URL based authentication request forwarding. A mountpoint configured with an authenticator
  will display a red key next to the mount point name on the admin screens.  
  You can read more about authentication and URL based source authentication [here](auth.html).

http-headers
: This element is placed anywhere inside the mount section of the icecast config. It will contain `<header>` child elements, that specify the actual HTTP headers one by one.

header
: This tag specifies the actual header to be sent to a HTTP client in response to every request for this mount point, but currently only if the HTTP status code is "200".
  This tag can contain the following attributes:

  - `name` is required and its value specifies the HTTP header field name.
  - `value` is required and its value specifies the HTTP header field value.

on-connect
: State a program that is run when the source is started. It is passed a parameter which is the name of the mountpoint that is starting.
  The processing of the stream does not wait for the script to end.
  Caution should be exercised as there is a small chance of stream file descriptors being mixed up with script file descriptors, if the FD numbers go above 1024. This will be further addressed in the next Icecast release.
  _This option is not available on Win32_

on-disconnect
: State a program that is run when the source ends. It is passed a parameter which is the name of the mountpoint that has ended.
  The processing of the stream does not wait for the script to end.  
  Caution should be exercised as there is a small chance of stream file descriptors being mixed up with script file descriptors, if the FD numbers go above 1024. This will be further addressed in the next Icecast release.
  _This option is not available on Win32_

# Path Settings

```xml
<paths>
    <basedir>./</basedir>
    <logdir>./logs</logdir>
    <pidfile>./icecast.pid</pidfile>
    <webroot>./web</webroot>
    <adminroot>./admin</adminroot>
    <allow-ip>/path/to/ip_allowlist</allow-ip>
    <deny-ip>/path_to_ip_denylist</deny-ip>
    <tls-certificate>/path/to/certificate.pem</tls-certificate>
    <ssl-allowed-ciphers>ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:ECDH+3DES:DH+3DES:RSA+AESGCM:RSA+AES:RSA+3DES:!aNULL:!MD5:!DSS</ssl-allowed-ciphers>
    <alias source="/foo" dest="/bar"/>
</paths>
```

This section contains paths which are used for various things within icecast. All paths (other than any aliases) should not end in a `/`.

basedir
: This path is used in conjunction with the chroot settings, and specifies the base directory that is chrooted to when the server is started.  
  _This feature is not supported on Win32._

logdir
: This path specifies the base directory used for logging. Both the `error.log` and `access.log` will be created relative to this directory.

pidfile
: This pathname specifies the file to write at startup and to remove at normal shutdown. The file contains the process id of the icecast process.  
  This could be read and used for sending signals to Icecast.

webroot
: This path specifies the base directory used for all static file requests. This directory can contain all standard file types
  (including mp3s and ogg vorbis files). For example, if webroot is set to `/var/share/icecast2`, and a request for
  `http://server:port/mp3/stuff.mp3` comes in, then the file `/var/share/icecast2/mp3/stuff.mp3` will be served.

adminroot
: This path specifies the base directory used for all admin requests. More specifically, this is used to hold the XSLT scripts used
  for the web-based admin interface. The admin directory contained within the icecast distribution contains these files.

allow-ip
: If specified, this points to the location of a file that contains a list of IP addresses that will be allowed to connect to Icecast.
  This could be useful in cases where a master only feeds known slaves.  
  The format of the file is simple, one IP per line.

deny-ip
: If specified, this points to the location of a file that contains a list of IP addressess that will be dropped immediately.
  This is mainly for problem clients when you have no access to any firewall configuration.  
  The format of the file is simple, one IP per line.

<!-- FIXME -->
alias
: Aliases are used to provide a way to create multiple mountpoints that refer to the same mountpoint.  
  For example: `<alias source="/foo" dest="/bar">`

tls-certificate
: If specified, this points to the location of a file that contains _both_ the X.509 private and public key.
  This is required for HTTPS support to be enabled. Please note that the user Icecast is running as must be able to read the file. Failing to ensure this will cause a "Invalid cert file" WARN message, just as if the file wasn't there.

tls-allowed-ciphers
: This optional tag specifies the list of allowed ciphers passed on to the SSL library.
  Icecast contains a set of defaults conforming to current best practices and you should _only_ override those, using this tag, if you know exactly what you are doing.

# Logging Settings

```xml
<logging>
    <accesslog>access.log</accesslog>
    <errorlog>error.log</errorlog>
    <playlistlog>playlist.log</playlistlog>
    <loglevel>4</loglevel> <!-- 4 Debug, 3 Info, 2 Warn, 1 Error -->
</logging>
```

This section contains information relating to logging within Icecast. There are three logfiles currently generated by Icecast,
an `error.log` (where all log messages are placed), an `access.log` (where all stream/admin/http requests are logged) and an
optional `playlist.log`.  
  
Note that on non-win32 platforms, a HUP signal can be sent to Icecast in which the log files are re-opened for appending giving the ability move/remove the log files.  
  
If you set any of the filenames to a simple dash (e.g. `<accesslog>-</accesslog>`) then Icecast will direct the log output to
STDERR instead of a file.

accesslog
: Into this file, all requests made to the icecast2 will be logged. This file is relative to the path specified by the `<logdir>` config value.

errorlog
: All Icecast generated log messages will be written to this file. If the loglevel is set too high (Debug for instance) then
  this file can grow fairly large over time. Currently, there is no log-rotation implemented.

playlistlog
: Into this file, a log of all metadata for each mountpoint will be written. The format of the logfile will most likely change over time
  as we narrow in on a standard format for this. Currently, the file is pipe delimited. This is optional and can be removed entirely
  from the config file.

logsize
: This value specifies (in Kbytes) the maxmimum size of any of the log files. When the logfile grows beyond this value, icecast will either
  rename it to `logfile.old`, or add a timestamp to the archived file (if logarchive is enabled).

logarchive
: If this value is set, then Icecast will append a timestamp to the end of the logfile name when logsize has been reached. If disabled, then
  the default behavior is to rename the logfile to `logfile.old` (overwriting any previously saved logfiles). We disable this by default to
  prevent the filling up of filesystems for people who don't care (or know) that their logs are growing.

loglevel
: Indicates what messages are logged by icecast. Log messages are categorized into one of 4 types, Debug, Info, Warn, and Error.  
    
  The following mapping can be used to set the appropriate value:
  
  -   loglevel = `4`: Debug, Info, Warn, Error messages are printed
  -   loglevel = `3`: Info, Warn, Error messages are printed
  -   loglevel = `2`: Warn, Error messages are printed
  -   loglevel = `1`: Error messages only are printed

# Security Settings
This section contains configuration settings that can be used to secure the icecast server by performing a chroot to a secured location or changing user and group on start-up. The latter allows icecast to bind to priviledged ports like 80 and 443, by being started as root and then dropping to the configured user/group after binding listener-sockets.

!!! attention
    This is currently not supported on Win32.

```xml
<security>
    <chroot>0</chroot>
    <changeowner>
        <user>nobody</user>
        <group>nogroup</group>
    </changeowner>
</security>
```

chroot
: An indicator which specifies whether a `chroot()` will be done when the server is started.
  The chrooted path is specified by the `<basedir>` configuration value.
  Setting up and using a chroot is an advanced concept and not in the scope of this document.

changeowner
: This section indicates the user and group that will own the icecast process when it is started.  
  These need to be valid users on the system. Icecast must be started as root for this to work.
