Icecast provides extensive run time statistics. Both in the form of active connection numbers and cumulative
counters (since server startup or respectively source connection startup).

# HTML Interface

Icecast comes with a HTML web interface, it exposes a basic set of server statistics that should
fulfil basic user needs. Icecast uses the very powerful libxslt engine to transform its internal
raw statistical data into custom tailored interfaces.  
Many people have written custom XSLT code that produces e.g. plain text “now playing”, XSPF, VCLT,
munin interface data, etc. If so desired, the files in webroot can be customized to contain more or less
information (see section on raw XML data below).

!!! attention
    __We strongly discourage attempts to scrape data from the web interface__ as we do not consider this an
    API and will change it, even completely, between versions! The preferred ways are custom XSLT, JSON and raw XML.

# JSON Stats

Since version 2.4.0 Icecast includes a basic JSON endpoint (`/status-json.xsl`) based on a xml2json template by Doeke Zanstra
(see `xml2json.xslt`). It exposes the same set of server statistics that are available through the web interface and
should fulfil basic user needs. The intention is to not break backwards compatibility of this interface in the future, 
still we recommend to design robust software that can deal with possible changes like addition or removal of variables.
Also note that not all variables are available all the time and availability may change at runtime due to stream type, etc.

# Available XML data

This section contains information about the raw XML server statistics data available inside Icecast. An example
stats XML tree will be shown and each element will be described. The following example stats tree will be used:  

```xml
<icestats>
	<admin>icemaster@localhost</admin>
	<client_connections>649</client_connections>
	<clients>2</clients>
	<connections>907</connections>
	<file_connections>379</file_connections>
	<host>localhost</host>
	<listener_connections>90</listener_connections>
	<listeners>0</listeners>
	<location>Earth</location>
	<server_id>Icecast 2.5</server_id>
	<source_client_connections>164</source_client_connections>
	<source_relay_connections>0</source_relay_connections>
	<source_total_connections>164</source_total_connections>
	<sources>2</sources>
	<stats>0</stats>
	<stats_connections>0</stats_connections>
	<source mount="/audio.ogg">
		<title>All that she wants</title>
		<artist>Ace of Base</artist>
		<audio_bitrate>499821</audio_bitrate>
		<audio_channels>2</audio_channels>
		<audio_info>samplerate=44100;quality=10%2e0;channels=2</audio_info>
		<audio_samplerate>44100</audio_samplerate>
		<channels>2</channels>
		<genre>various</genre>
		<ice-bitrate>499</ice-bitrate>
		<listener_peak>0</listener_peak>
		<listeners>0</listeners>
		<listenurl>http://localhost:8000/audio</listenurl>
		<max_listeners>unlimited</max_listeners>
		<public>1</public>
		<quality>10.0</quality>
		<samplerate>44100</samplerate>
		<server_description>Teststream</server_description>
		<server_name>Great audio stream</server_name>
		<server_type>application/ogg</server_type>
		<server_url>http://example.org/</server_url>
		<slow_listeners>0</slow_listeners>
		<source_ip>192.0.2.21</source_ip>
		<subtype>Vorbis</subtype>
		<total_bytes_read>3372153</total_bytes_read>
		<total_bytes_sent>0</total_bytes_sent>
		<user_agent>LadioCast/0.10.5 libshout/2.3.1</user_agent>
	</source>
	<source mount="/video.ogg">
		<audio_bitrate>276000</audio_bitrate>
		<audio_channels>6</audio_channels>
		<audio_samplerate>48000</audio_samplerate>
		<frame_rate>25.00</frame_rate>
		<frame_size>720 x 576</frame_size>
		<genre>various</genre>
		<ice-bitrate>276</ice-bitrate>
		<listener_peak>0</listener_peak>
		<listeners>0</listeners>
		<listenurl>http://localhost:8000/video</listenurl>
		<max_listeners>unlimited</max_listeners>
		<public>0</public>
		<server_description>Unspecified description</server_description>
		<server_name>Unspecified name</server_name>
		<server_type>video/ogg</server_type>
		<slow_listeners>0</slow_listeners>
		<source_ip>192.0.2.21</source_ip>
		<subtype>Vorbis/Theora</subtype>
		<title>ERAGON</title>
		<total_bytes_read>37136</total_bytes_read>
		<total_bytes_sent>0</total_bytes_sent>
		<user_agent>Lavf/55.20.0</user_agent>
		<video_bitrate>200000</video_bitrate>
		<video_quality>0</video_quality>
	</source>
</icestats>
```

## General Statistics
<!-- FIXME -->

admin
: As set in the server config, this should contain contact details for getting in touch with the server administrator.
  Usually this will be an email address, but as this can be an arbitrary string it could also be a phone number.

client_connections
: Client connections are basically anything that is not a source connection. These include listeners (not concurrent,
  but cumulative), any admin function accesses, and any static content (file serving) accesses.
  _This is an accumulating counter._

clients
: Number of currently active client connections.

connections
: The total of all inbound TCP connections since start-up.
  _This is an accumulating counter._

file_connections
: _This is an accumulating counter._

host
: As set in the server config, this should be the full DNS resolveable name or FQDN for the host on which this
  Icecast instance is running.

listener_connections
: Number of listener connections to mount points.
  _This is an accumulating counter._

listeners
: Number of currently active listener connections.

location
: As set in the server config, this is a free form field that should describe e.g. the physical location of this server.

server_id
: Defaults to the version string of the currently running Icecast server. While not recommended it can be overriden in
  the server config.

server_start_iso8601
: Timestamp of server startup in ISO 8601 date format.

server_start
: Timestamp of server startup in RFC 2822 date format. This field is deprecated and may be removed in a future version,
  please use `server_start_iso8601` instead.

source_client_connections
: Source client connections are the number of times (cumulative since start-up, not just currently connected) a source
  client has connected to Icecast.
  _This is an accumulating counter._

source_relay_connections
: Number of outbound relay connections to (master) icecast servers.
  _This is an accumulating counter._

source_total_connections
: Both clients and relays.
  _This is an accumulating counter._

sources
: The total of currently connected sources.

stats
: The total of currently connected STATS clients.

stats_connections
: Number of times a stats client has connected to Icecast.
  _This is an accumulating counter._

## Source-specific Statistics
Please note that the statistics are valid within the scope of the current source connection.
A reconnect or disconnection will reset those.

artist
: Artist of the current song
  _Metadata set by source client_

title
: Title of the current song
  _Metadata set by source client_

audio_bitrate
: Audio bitrate in bits/s
  _Can be set by source client_

audio_channels
: Number of audio channels.

audio-info
: Information about the bitrate/samplerate/quality of the stream.
  Also used for directory listings.
  _Metadata set by source client_
  Example:
  `samplerate=44100;quality=10%2e0;channels=2` (LadioCast)
  `ice-bitrate=128;ice-channels=2;ice-samplerate=44100` (Butt)

ice-bitrate
: Information about the audio bitrate (in kbit/s) of the stream.
  _Can be set by source client_

samplerate
: Information about the samplerate of the stream.
  _Can be set by source client_

quality
: Information about the audio quality of the stream.
  _Metadata set by source client_

frame_rate
: Information about the framerate of the stream.
  _Only present for video streams_

frame_size
: Information about the frame size of the stream.
  _Only present for video streams_

video_bitrate
: Information about the video bitrate of the stream.
  _Only present for video streams_

video_quality
: Information about the video quality of the stream.
  _Only present for video streams_

server_name
: Stream name
  _Metadata set by source client_

server_description
: Stream description
  _Metadata set by source client_

server_type
: MIME-type for the stream currently active on this mountpoint.

subtype
: MIME-subtype, can be e.g. codecs like Opus, Vorbis, Theora.
  Separated with `/`.

listener_peak
: Peak concurrent number of listener connections for this mountpoint.

listeners
: The number of currently connected listeners.

listenurl
: URL to this mountpoint. (This is not aware of aliases)

max_listeners
: Maximum number of listeners permitted to concurrently connect to this mountpoint.

public
: Flag that indicates whether this mount is to be listed on a directory.
  _Set by source client, can be overriden by server config_

slow_listeners
: Number of slow listeners

source_ip
: IP address of the currently connected source client.
  In case of relays the content of `<server>`.

stream_start_iso8601
: Timestamp of when the currently active source client connected to this mount point in ISO 8601 date format.

stream_start
: Timestamp of when the currently active source client connected to this mount point in RFC 2822 date format.
  This field is deprecated and may be removed in a future version, please use `stream_start_iso8601` instead.

total_bytes_read
: Total number of bytes received from the source client.

total_bytes_sent
: Total number of bytes sent to all listener connections since last source connect.

user_agent
: HTTP user agent string as sent by the source client.


Additional data can be accessed through the admin interface, as every page of the admin
interface has an XML equivalent.
