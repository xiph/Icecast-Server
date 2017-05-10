This section contains information about the admin interface of Icecast. Through this interface the user can manipulate many server features. From it you can gather statistics, move listeners from one mountpoint to another, disconnect connected sources or listeners and many other activities. Each function is enumerated here as well as an example usage of the function.

Each of these functions requires HTTP authentication via the appropriate username and password. For mount-specific functions, you may use either the `<admin-username>` and `<admin-password>` specified in the Icecast config file, or the username and password specified for that mountpoint (if any). For general functions (not specific to a single mountpoint), you must use the admin username and password.

# Admin Functions (mount specific)
All these admin functions are mount specific in that they only apply to a particular mountpoint
(as opposed to applying to the entire server). Each of these functions requires a mountpoint to
be specified as input.

## Metadata Update
This function provides the ability for either a source client or any external program to update
the metadata information for a particular mountpoint.

Example:  
`/admin/metadata?mount=/stream&mode=updinfo&song=ACDC+Back+In+Black`

## Fallback Update
This function provides the ability for either a source client or any external program to update the
“fallback mountpoint” for a particular mountpoint. Fallback mounts are those that are used in the even
of a source client disconnection. If a source client disconnects for some reason that all currently
connected clients are sent immediately to the fallback mountpoint.

Example:  
`/admin/fallbacks?mount=/stream.ogg&fallback=/fallback.ogg`

## List Clients
This function lists all the clients currently connected to a specific mountpoint. The results are sent
back in XML form.

Example:  
`/admin/listclients?mount=/stream.ogg`

## Move Clients (Listeners)
This function provides the ability to migrate currently connected listeners from one mountpoint to another.
This function requires 2 mountpoints to be passed in: mount (the *from* mountpoint) and destination
(the _to_ mountpoint). After processing this function all currently connected listeners on mount will
be connected to destination. Note that the destination mountpoint must exist and have a sounce client
already feeding it a stream.

Example:  
`/admin/moveclients?mount=/stream.ogg&destination=/newstream.ogg`

## Kill Client (Listener)
This function provides the ability to disconnect a specific listener of a currently connected mountpoint.
Listeners are identified by a unique id that can be retrieved by via the “List Clients” admin function.
This id must be passed in to the request via the variable `id`. After processing this request, the listener will no longer be
connected to the mountpoint.

Example:  
`/admin/killclient?mount=/mystream.ogg&id=21`

## Kill Source
This function will provide the ability to disconnect a specific mountpoint from the server. The mountpoint
to be disconnected is specified via the variable `mount`.

Example:  
`/admin/killsource?mount=/mystream.ogg`


# Admin Functions (general)

## Stats
The stats function provides the ability to query the internal statistics kept by the Icecast server.
Almost all information about the internal workings of the server such as the mountpoints connected,
how many client requests have been served, how many listeners for each mountpoint, etc. are available
via this admin function.

Example:  
`/admin/stats`

## List Mounts
The list mounts function provides the ability to view all the currently connected mountpoints.

Example:  
`/admin/listmounts`

# Web-Based Admin Interface
As an alternative to manually invoking these URLs, there is a web-based admin interface.
This interface provides the same functions that were identified and described above but presents them in
a nicer way. The web-based admin Interface to Icecast is shipped with Icecast provided in the
`admin` directory and comes ready to use.

The main path for the Web-Based Admin Interface is:  
`/admin/stats.xsl`

## Advanced

The web-based admin interface is a series of XSL-Transform files which are used to display all the XML obtained
via the URL admin interface. This can be changed and modified to suit the user's need. Knowledge of
XSLT and transformations from XML to HTML are required in order to make changes to these scripts.

__Modification of existing XSLT transforms in `/admin` is allowed, but new files cannot be created here.__

Creation of new XSLT transforms as well as modification of existing transforms is allowed in  the `/web` directory.
These work using the document returned by the `/admin/stats` endpoint.  
To see the XML document that is applied to each admin XSLT, just remove the .xsl in your request
(i.e. `/admin/listclients`). You can then code your XSL transform accordingly.