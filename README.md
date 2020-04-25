
# Dire Wolf - Edge of Space Sciences (EOSS) Branch #

## About This Branch ###

This is a customized branch of Dire Wolf to enable reception of [APRS](http://www.aprs.org/) packets within the EOSS Tracker system for tracking high altitude balloons.  The EOSS Tracker system leverages several open source projects to provide a graphical (web-based), near real-time, system that enhances tracking and recovery efforts with APRS enabled HAB flights.  Information on the mainstream Dire Wolf branch can be found [here](direwolf-README.md).  

<img src="eoss-block-diagram.png" alt="EOSS Tracker Block Diagram" width="800">

There are two primary differences between this EOSS specific branch and the mainstream Dire Wolf distribution::
- PostgreSQL integration so that incoming packets are saved to a database table.
- Increased channel limits over the default of 3

These changes allow Dire Wolf to be come the primary RF receive mechanism for getting APRS packets into the EOSS Tracker system.  Having database connectivity allows the overall system to have a history of heard APRS packets that is indexed and searchable. 


## Platforms and Build Instructions ###

Given that the EOSS Tracker system is based on Ubuntu Linux no effort has been made to ensure other platforms (ex. Windows, Mac OS X, etc.) can properly build and run this branch.  However, it "should" compile on other platforms if care is taken to get PostgreSQL libraries and include files accounted for.

### Ubuntu build instructions ###

There are several prerequisite packages needed in order to compile the branch:

    apt-get -y install postgresql postgresql-contrib postgis postgresql-10-postgis-2.4 postgresql-10-postgis-scripts libpq-dev
    

Use git to download the most recent release of the EOSS branch:

	cd ~
	git clone https://www.github.com/edgeofspace/direwolf
	cd direwolf
    git checkout eoss


Build the binaries:

	make
	sudo make install
	make install-conf


### The Underlying Database Table ###

This branch of Dire Wolf will attempt to save each incoming packet as a row within a database table.  It will create that table automatically so there is no need for creating a 
table beforehand.  There are a couple of important prerequisites for proper operation:
- The [PostGIS](http://www.postgis.org/) extention should be added to your database
- Database credentials (username & password) that direwolf can use for database connectivity which should have permissions to create tables.

Upon startup, Dire Wolf will attempt to create the following table and indices (one doesn't need to create this...only included here for reference):

    CREATE TABLE public.dw_packets (
        instance integer NOT NULL,
        channel integer NOT NULL,
        tm timestamp with time zone NOT NULL,
        sdr integer,
        freq integer,
        callsign text NOT NULL,
        heardfrom text,
        sourcename text,
        source_symbol text,
        speed_mph numeric,
        bearing numeric,
        altitude numeric,
        manufacturer text,
        status text,
        telemetry text,
        comment text,
        location2d public.geometry(Point,4326),
        location3d public.geometry(PointZ,4326),
        raw text,
        hash text,
        receive_level integer,
        mark_level integer,
        space_level integer
    );
    
    ALTER TABLE ONLY public.dw_packets ADD CONSTRAINT dw_packets_pkey PRIMARY KEY (instance, channel, tm, callsign);
    CREATE INDEX dw_packets_idx1 ON public.dw_packets USING btree (callsign);
    CREATE INDEX dw_packets_idx3 ON public.dw_packets USING btree (hash);
    CREATE INDEX dw_packets_idx4 ON public.dw_packets USING btree (freq);


## Additional Configuration Options ##

There are several additional options that should be appended to one's direwolf.conf configuration file for specifying database connections, etc..  For example:

    ..
    ..
    ..
    INSTANCE 0
    PGUSER dbusername
    PGPASSWORD dbpassword
    PGDBNAME dbname
    FREQMAP  0 0 144390000 0 2 144340000 0 4 145825000 1 6 144390000 1 8 144340000 1 10 145825000


### INSTANCE ###

The `INSTANCE` variable should be an integer that defines the unique instance of direwolf running on a user's system.  It's certainly possible to have multiple copies of direwolf running on the same system, all saving packets to the `dw_packets` database table.  The instance number ensures that each of these separate direwolf instances is uniquely defined within the database.

    INSTANCE <integer>


### PGUSER, PGPASSWORD, PGDBNAME ###

The `PGUSER` and `PGPASSWORD` options specify a database user and accompanying password with login rights as well as table creation rights to the database defined by `PGDBNAME`.  Do not use quotation marks around the username, password, or database name.

    PGUSER <string>
    PGPASSWORD <string>
    PGDBNAME <string>


### FREQMAP ###

The `FREQMAP` option is used to marry up an individual direwolf channel to the frequency being listed to on the radio or SDR.  Each direwolf channel is uniquely enumerated 
(within the direwolf.conf), however, when there are multiple incoming audio streams we need a way identify which stream corresponds to which RF frequency.  

    FREQMAP <integer> <integer> <integer>
    FREQMAP <sdr or radio number> <direwolf channel number> <frequency in Hz>

When defining the channels that direwolf will listen for audio streams from, the `CHANNEL` option is used to identify each one.  The `CHANNEL` number is incremented by two for each successive channel.  For example, a common direwolf confguration file might look something like this:

    ..
    ..
    ADEVICE0 udp:12000 null
    ARATE 48000
    ACHANNELS 1
    CHANNEL 0
    MYCALL N0CALL
    MODEM 1200
    FIX_BITS 0

    ADEVICE1 udp:12001 null
    ARATE 48000
    ACHANNELS 1
    CHANNEL 2
    MYCALL N0CALL
    MODEM 1200
    FIX_BITS 0

    ADEVICE2 udp:12010 null
    ARATE 48000
    ACHANNELS 1
    CHANNEL 4
    MYCALL N0CALL
    MODEM 1200
    FIX_BITS 0

    ADEVICE3 udp:12011 null
    ARATE 48000
    ACHANNELS 1
    CHANNEL 6
    MYCALL N0CALL
    MODEM 1200
    FIX_BITS 0
    ..
    ..

To build a `FREQMAP` entry for this configuration we need to know two additional bits of information: 1) what sdr or radio is sending audio for each stream, and 2) what frequency are 
they receving on.  With the direwolf.conf file shown above, let's assume we have two SDR systems, each listening on two frequencies for APRS packets.  Maybe we have a system something 
like this:

<img src="example-sdr-setup.png" alt="Example SDR Rx Setup" width="800">

For the first channel, `ADEVICE0`, our `FREQMAP` option would look like this for SDR #0, channel 0, and a frequency of 144.39MHz:

    FREQMAP 0 0 144390000


Continuing on to the second Dire Wolf channel for SDR #0, channel 2, and a frequency of 144.340MHz:

    FREQMAP 0 0 144390000 0 2 144340000


And completing the option we end up with the `FREQMAP` option looking like this for all four channels:


    FREQMAP 0 0 144390000 0 2 144340000 1 4 144390000 1 6 144825000


