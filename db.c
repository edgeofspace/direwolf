//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015  John Langner, WB2OSZ
//    Copyright (C) 2017        Jeff Deaton, N6BA
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


/*------------------------------------------------------------------
 *
 * File:	db.c
 *
 * Purpose:	Add received APRS packets to a PostgresQL database
 *
 * Description:  For those packets that we receive, we want to add them to the
 *		 dw_packets table.  
 *
 *               The database table, dw_packets, is expected to have these columns:
 *                       Column     |           Type           | Modifiers 
 *                   ---------------+--------------------------+-----------
 *                    instance      | integer                  | 
 *                    channel       | integer                  | 
 *                    tm            | timestamp with time zone | 
 *                    callsign      | text                     | 
 *                    heardfrom     | text                     | 
 *                    sourcename    | text                     | 
 *                    source_symbol | text                     | 
 *                    speed_mph     | numeric                  | 
 *                    bearing       | numeric                  | 
 *                    altitude      | numeric                  | 
 *                    manufacturer  | text                     | 
 *                    status        | text                     | 
 *                    telemetry     | text                     | 
 *                    comment       | text                     | 
 *                    location2d    | geometry(Point,4326)     | 
 *                    location3d    | geometry(PointZ,4326)    | 
 *                    raw           | text                     | 
 *
 *
 *                           Table "public.packets"
 *                         Column   |           Type           | Collation | Nullable | Default 
 *                      ------------+--------------------------+-----------+----------+---------
 *                       tm         | timestamp with time zone |           | not null | 
 *                       callsign   | text                     |           | not null | 
 *                       symbol     | text                     |           |          | 
 *                       speed_mph  | numeric                  |           |          | 
 *                       bearing    | numeric                  |           |          | 
 *                       altitude   | numeric                  |           |          | 
 *                       comment    | text                     |           |          | 
 *                       location2d | geometry(Point,4326)     |           |          | 
 *                       location3d | geometry(PointZ,4326)    |           |          | 
 *                       raw        | text                     |           |          | 
 *                       ptype      | text                     |           |          | 
 *                       hash       | text                     |           | not null | 
 *
 *------------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>	
#include <string.h>	
#include <ctype.h>	
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// This is for postgresql
#ifdef __APPLE__
#include </opt/local/include/postgresql96/libpq-fe.h>
#else
#include <postgresql/libpq-fe.h>
#endif

#include "ax25_pad.h"
#include "textcolor.h"
#include "decode_aprs.h"
#include "xid.h"
#include "config.h"
#include "db.h"


#define MAX_SAFE 500

static PGconn *db_connection;
static int DWInstance; 
static struct freqmap_s *freqmap;


/*------------------------------------------------------------------
 *
 * Function:	db_init
 *
 * Purpose:	Initialization at start of application for creating database connections.
 *
 * Inputs:	the misc_config struct that contains all of the parameters we're interested in.
 *
 * Global Out:	db_connection 	- PostgresQL database connection object.
 *
 *------------------------------------------------------------------*/



void db_init (struct misc_config_s *misc_config)
{
       char connection_string[2048];
       char host_string[1024];
       char password_string[1024];
       bool okay = true;

       if (strlen(misc_config->pghost) > 0)
           sprintf (host_string, "sslmode=require host=%s", misc_config->pghost);
       else
           strlcpy (host_string, "\0", sizeof("\0"));

       if (strlen(misc_config->pgpassword) > 0)
           sprintf (password_string, "password=%s", misc_config->pgpassword);
       else
           strlcpy (password_string, "\0", sizeof("\0"));

       if (misc_config->pgport == 0)  
           sprintf (connection_string, "user=%s %s %s dbname=%s", misc_config->pguser, (strlen(host_string) > 0 ? host_string : " "), (strlen(password_string) > 0 ? password_string : " "), misc_config->pgdbname);
       else
           sprintf (connection_string, "user=%s %s port=%d %s dbname=%s", misc_config->pguser, (strlen(host_string) > 0 ? host_string : " "), misc_config->pgport, (strlen(password_string) > 0 ? password_string : " "), misc_config->pgdbname);

       // Check the database name
       if (strlen(misc_config->pgdbname) <= 0) {
	       text_color_set(DW_COLOR_ERROR);
	       dw_printf ("Database name has zero length.\n");
           okay = false;
       }

       // Check the database username
       if (strlen(misc_config->pguser) <= 0) {
	       text_color_set(DW_COLOR_ERROR);
	       dw_printf ("Database username has zero length.\n");
           okay = false;
       }

       // Check the database password
       if (strlen(misc_config->pgpassword) <= 0) {
	       text_color_set(DW_COLOR_ERROR);
	       dw_printf ("Database password has zero length.\n");
           okay = false;
       }

       
       // connect to the database...
       if (okay) {
           db_connection = PQconnectdb(connection_string);

           if (PQstatus(db_connection) != CONNECTION_OK) {
               text_color_set(DW_COLOR_ERROR);
               dw_printf ("Unable to connect to database, %s, %s.\n", misc_config->pgdbname, PQerrorMessage(db_connection));
               db_term();
           } else {
               text_color_set(DW_COLOR_INFO);
               dw_printf ("Connection to database, %s, successful.\n", misc_config->pgdbname);
               dw_printf ("Database client_encoding status:  %s.\n", PQparameterStatus(db_connection, "client_encoding"));

               // Now check if the dw_packets table exists...if there was an error, then we close our DB connection.
               if (!check_dw_packets_table()) {
                   db_term();
               }
           }
           
           DWInstance = misc_config->direwolf_instance;
           freqmap = misc_config->freqmap;
       }
}


/*------------------------------------------------------------------
 *
 * Function:	check_dw_packets_table
 *
 * Purpose:	This function will check for the existing of the dw_packet table and if not present, create it.
 *
 * Inputs:	 none.
 *
 *------------------------------------------------------------------*/

bool check_dw_packets_table (void) {
    int num_rows;
    bool status = false;
    char *result_str;
    PGresult *res;
    ExecStatusType resultStatus;

    char *rec_value;
    char *mark_value;
    char *space_value;

    char rec_sql[] = "alter table dw_packets add column receive_level int;";
    char mark_sql[] = "alter table dw_packets add column mark_level int;";
    char space_sql[] = "alter table dw_packets add column space_level int;";

    char query[1024];
    char dw_packets[] = "create table dw_packets ( instance int, channel int, tm timestamp with time zone, sdr int, freq int, callsign text, heardfrom text, sourcename text, source_symbol text, speed_mph decimal, bearing decimal, altitude decimal, manufacturer text, status text, telemetry text, comment text, location2d geometry(POINT, 4326), location3d geometry(POINTZ, 4326), raw text, hash text, receive_level int, mark_level int, space_level int, primary key (instance, channel, tm, callsign)); create index dw_packets_idx1 on dw_packets (callsign); create index dw_packets_idx3 on dw_packets (hash); create index dw_packets_idx4 on dw_packets(freq);";

    strlcpy(query, "", sizeof(query));
    snprintf (query, sizeof(query), "select exists(select * from information_schema.tables where table_name = 'dw_packets');");
    
    /* Execute the SQL statement */
    res = PQexec(db_connection, query);
    resultStatus = PQresultStatus(res);

    /* Check return code */
    if (resultStatus != PGRES_TUPLES_OK && resultStatus != PGRES_SINGLE_TUPLE) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf ("Error checking if table dw_packets exists:  %s.\n", PQerrorMessage(db_connection));
        dw_printf ("SQL:  %s.\n", query);
    }
    else {
        // we determine if the table row return was "t" or "f"
        num_rows = PQntuples(res);

        if (num_rows > 0) {
            // evaluate and create the table if needed 
            result_str = PQgetvalue(res, 0, 0);
            if (result_str[0] == 'f') { // the table does not exist...
                // create the table
               
                /* Clear prior result */ 
                PQclear(res);

                strlcpy(query, "", sizeof(query));
                snprintf (query, sizeof(query), "%s", dw_packets);
     
                /* Execute the SQL statement to create the table */
                res = PQexec(db_connection, query);

                /* Check return code */
                if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                    text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Error creating table dw_packets:  %s.\n", PQerrorMessage(db_connection));
                    dw_printf ("SQL:  %s.\n", query);
                }
                else {
                    text_color_set(DW_COLOR_INFO);
                    dw_printf ("dw_packets table created successfully\n");
                    status = true;
                }
            }
            else { // the table exists...
                /* Check if the table has the audio level columns: receive_level, mark_level, space_level. */
               
                /* Clear prior result */ 
                PQclear(res);
              
                /* query to check if the columns exist */
                strlcpy(query, "", sizeof(query));
                snprintf (query, sizeof(query), "select (SELECT EXISTS (SELECT * FROM information_schema.columns WHERE table_name='dw_packets' AND column_name='receive_level')) as rec, (SELECT EXISTS (SELECT * FROM information_schema.columns WHERE table_name='dw_packets' AND column_name='mark_level')) as mark, (SELECT EXISTS (SELECT * FROM information_schema.columns WHERE table_name='dw_packets' AND column_name='space_level')) as space;");

                /* Execute the SQL statement */
                res = PQexec(db_connection, query);
                resultStatus = PQresultStatus(res);
                
                /* Check return code of column check */
                if (resultStatus != PGRES_TUPLES_OK && resultStatus != PGRES_SINGLE_TUPLE) {
                    text_color_set(DW_COLOR_ERROR);
                    dw_printf ("PQresultStatus:  %s.\n", PQresStatus(resultStatus));
                    dw_printf ("Error checking if table columns for dw_packets exist: %s.\n", PQerrorMessage(db_connection));
                    dw_printf ("SQL:  %s.\n", query);
                }
                else {
                    // we determine if the table row return was "t" or "f"
                    num_rows = PQntuples(res);

                    if (num_rows > 0) {
                        // evaluate if columns exist
                        rec_value = PQgetvalue(res, 0, 0);
                        mark_value = PQgetvalue(res, 0, 1);
                        space_value = PQgetvalue(res, 0, 2);

                        /* prepare the query */
                        strlcpy(query, "", sizeof(query));
                        if (rec_value[0] == 'f') 
                           strlcat(query, rec_sql, sizeof(query));
                        if (mark_value[0] == 'f') 
                           strlcat(query, mark_sql, sizeof(query));
                        if (space_value[0] == 'f') 
                           strlcat(query, space_sql, sizeof(query));


                        /* if any of these columns are missing, then run the query to add them to the dw_packets table */
                        if (rec_value[0] == 'f' || mark_value[0] == 'f' || space_value[0] == 'f') {
                            /* Clear prior result */ 
                            PQclear(res);

                            /* Execute the SQL statement to create the table */
                            res = PQexec(db_connection, query);

                            /* Check return code */
                            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                                text_color_set(DW_COLOR_ERROR);
                                dw_printf ("Error trying to add table columns for dw_packets :  %s.\n", PQerrorMessage(db_connection));
                                dw_printf ("SQL:  %s.\n", query);
                            }
                            else {
                                text_color_set(DW_COLOR_INFO);
                                dw_printf ("columns added to dw_packets table successfully\n");
                                status = true;
                            }
                        }
                        else // the columns already exist...all good.
                            status = true;
                    }
                    else { // num_rows > 0
                        text_color_set(DW_COLOR_ERROR);
                        dw_printf ("Error:  unable to get a list of columns for the dw_packets table from the database.");
                    }   
                }  /* Check return code of column check */
            } // the table exists...
        }
        else {
            text_color_set(DW_COLOR_ERROR);
            dw_printf ("Error:  unable to get a list of tables from the database.");
        }
    }

    /* Clear result */
    PQclear(res);

    return status;

}



/*------------------------------------------------------------------
 *
 * Function:	db_write_recv
 *
 * Purpose:	Add a row to the dw_packets table.
 *
 * Inputs:	
 *
 *      chan     - Radio channel where heard.
 *
 *		A        - Explode information from APRS packet.
 *
 *		pp       - Received packet object.
 *
 *		alevel   - audio levels
 *
 *------------------------------------------------------------------*/

void db_write_recv (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel)
{
	char heard[AX25_MAX_ADDR_LEN+1];
	int h;
    int i;
    struct freqmap_s *ptr;
	char stemp[MAX_SAFE+1];
	char tempbuffer[MAX_SAFE*6+1];
	char packettext[MAX_SAFE*6+1];
	unsigned char *pinfo;
	int info_len;

	char slat[16], slon[16], sspd[12], scse[12], salt[16];
	char sname[24];
	char ssymbol[8];
	char smfr[60];
	char sstatus[40];
	char stelemetry[256];
	char scomment[256];
    char sql_insert_string[2048];
    bool coords_valid = true;
    char geom_string[1024];
    char geom_string3d[2048];
	char infopart[MAX_SAFE*6+1];

    char *e_ssymbol;
    char *e_smfr;
    char *e_sstatus;
    char *e_stelemetry;
    char *e_scomment;
    char *e_packettext;
    char *e_infopart;


	assert (pp != NULL);


    /* Connect to the PostgresQL database */
    if (PQstatus(db_connection) != CONNECTION_OK) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Connection to database failed:  %s.\n", PQerrorMessage(db_connection));
	    return;
    }


    info_len = ax25_get_info (pp, &pinfo);
    ax25_format_addrs (pp, stemp);

    // Zero out these arrays
    strlcpy(packettext, "", sizeof(packettext));
    strlcpy(tempbuffer, "", sizeof(tempbuffer));
    strlcpy(packettext, stemp, sizeof(packettext));
    strlcpy(infopart, "", sizeof(infopart));


    /* Demystify non-APRS.  Use same format for transmitted frames in xmit.c. */
    if ( ! ax25_is_aprs(pp)) {
          ax25_frame_type_t ftype;
          cmdres_t cr;
          char desc[80];
          int pf;
          int nr;
          int ns;

          ftype = ax25_frame_type (pp, &cr, desc, &pf, &nr, &ns);

          /* Could change by 1, since earlier call, if we guess at modulo 128. */
          info_len = ax25_get_info (pp, &pinfo);

          if (ftype == frame_type_U_XID) {
              struct xid_param_s param;
              char info2text[100];

              xid_parse (pinfo, info_len, &param, info2text, sizeof(info2text));

              /* For creating the text string from this packet for sending to the "raw" packet to a database */
	          //strlcat(packettext, make_ascii_only((char *) info2text, info_len, (char *) &tempbuffer, sizeof(tempbuffer)), sizeof(packettext));
	          strlcat(packettext, info2text, sizeof(packettext));

          }
          else {
              /* For creating the text string from this packet for sending to the "raw" packet to a database */
              //strlcat(packettext, make_ascii_only((char *)pinfo, info_len, (char *) &tempbuffer, sizeof(tempbuffer)), sizeof(packettext));
	          strlcat(packettext, (char *) pinfo, sizeof(packettext));
          }
    }
    else {
        /* For creating the text string from this packet for sending to the "raw" packet to a database */
        //strlcat(packettext, make_ascii_only((char *)pinfo, info_len, (char *) &tempbuffer, sizeof(tempbuffer)), sizeof(packettext));
        strlcat(packettext, (char *) pinfo, sizeof(packettext));
    }

    // Trim off ending carriage returns and newline characters from the raw packet.
    trim(packettext);

    // Copy the contents of pinfo (which after the above...should contain the info part of the APRS packet) into the infopart array
    strlcpy(infopart, (char *) pinfo, info_len+1);
    trim(infopart);


    /* Who are we hearing?   Original station or digipeater? */
	/* Similar code in direwolf.c.  Combine into one function? */
	strlcpy(heard, "", sizeof(heard));
	if (pp != NULL) {
	    if (ax25_get_num_addr(pp) == 0) {
	        /* Not AX.25. No station to display below. */
	        h = -1;
	        strlcpy (heard, "", sizeof(heard));
	    }
	    else {
	        h = ax25_get_heard(pp);
            ax25_get_addr_with_ssid(pp, h, heard);
	    }
	 
	    if (h >= AX25_REPEATER_2 && strncmp(heard, "WIDE", 4) == 0 && isdigit(heard[4]) && heard[5] == '\0') {
            ax25_get_addr_with_ssid(pp, h-1, heard);
            //strlcat (heard, "?", sizeof(heard));
        }
    }

    /* Initialize all of these strings */
    strlcpy (sname, "", sizeof(sname));
    strlcpy (ssymbol, "", sizeof(ssymbol));
    strlcpy (smfr, "", sizeof(smfr));
    strlcpy (sstatus, "", sizeof(sstatus));
    strlcpy (stelemetry, "", sizeof(stelemetry));
    strlcpy (scomment, "", sizeof(scomment));

	/* copy values into strings for indvidual items */
    strlcpy (sname, ((strlen(A->g_name) > 0) ? A->g_name : A->g_src), sizeof(sname));  
    ssymbol[0] = A->g_symbol_table;
    ssymbol[1] = A->g_symbol_code;
    ssymbol[2] = '\0';
    strlcpy (smfr, A->g_mfr, sizeof(smfr));
    strlcpy (sstatus, A->g_mic_e_status, sizeof(sstatus));
    strlcpy (stelemetry, A->g_telemetry, sizeof(stelemetry));
    strlcpy (scomment, A->g_comment, sizeof(scomment));
	//e_stelemetry = make_ascii_only(A->g_telemetry, strlen(A->g_telemetry), (char *) &stelemetry, sizeof(stelemetry));
	//e_scomment = make_ascii_only(A->g_comment, strlen(A->g_comment), (char *) &scomment, sizeof(scomment));
	strlcpy (slat, "", sizeof(slat));  if (A->g_lat != G_UNKNOWN)         snprintf (slat, sizeof(slat), "%.6f", A->g_lat);  else coords_valid = false;
	strlcpy (slon, "", sizeof(slon));  if (A->g_lon != G_UNKNOWN)         snprintf (slon, sizeof(slon), "%.6f", A->g_lon);  else coords_valid = false;
	strlcpy (sspd, "", sizeof(sspd));  snprintf (sspd, sizeof(sspd), "%.1f", (A->g_speed_mph != G_UNKNOWN ? A->g_speed_mph : 0));
	strlcpy (scse, "", sizeof(scse));  snprintf (scse, sizeof(scse), "%.1f", (A->g_course != G_UNKNOWN ? A->g_course : 0));
	strlcpy (salt, "", sizeof(salt));  snprintf (salt, sizeof(salt), "%.2f", (A->g_altitude_ft != G_UNKNOWN ? A->g_altitude_ft : 0.00));
    strlcpy(geom_string, "", sizeof(geom_string));
    strlcpy(geom_string3d, "", sizeof(geom_string3d));
    strlcpy(sql_insert_string, "", sizeof(sql_insert_string));

	/* Create the SQL statement for dealing with the GIS 2D and 3D locations/coords along with altitude in meters */
    if (coords_valid == FALSE || (A->g_lat == 0 && A->g_lon == 0)) { 
        snprintf (geom_string, sizeof(geom_string), "NULL");
        snprintf (geom_string3d, sizeof(geom_string3d), "NULL");
    }
    else {
        snprintf (geom_string, sizeof(geom_string), "ST_GeometryFromText('POINT(%s %s)', 4326)", slon, slat);
        if (A->g_altitude_ft != G_UNKNOWN && A->g_altitude_ft != 0)
            snprintf (geom_string3d, sizeof(geom_string3d), "ST_GeometryFromText('POINTZ(%s %s %.2f)', 4326)", slon, slat, DW_FEET_TO_METERS(A->g_altitude_ft));
        else
            snprintf (geom_string3d, sizeof(geom_string3d), "NULL");
    }

    /* Escape any characters */
    e_ssymbol = PQescapeLiteral(db_connection, ssymbol, sizeof(ssymbol));
    e_smfr = PQescapeLiteral(db_connection, smfr, sizeof(smfr));
    e_sstatus = PQescapeLiteral(db_connection, sstatus, sizeof(sstatus));
    e_stelemetry = PQescapeLiteral(db_connection, stelemetry, sizeof(stelemetry));
    e_scomment = PQescapeLiteral(db_connection, scomment, sizeof(scomment));
	e_packettext = PQescapeLiteral(db_connection, packettext, sizeof(packettext));
	e_infopart = PQescapeLiteral(db_connection, infopart, sizeof(infopart));


    /* Find which sdr and frequency cooresponds to this channel */
    i = 0;
    ptr = freqmap;
    while (i < MAX_FREQMAP && ptr->channel != chan) {
        ptr++;
        i++;
    }

    /* Create the SQL insert command... */
	snprintf (sql_insert_string, sizeof(sql_insert_string), "insert into dw_packets (instance, channel, tm, sdr, freq, callsign, heardfrom, sourcename, source_symbol, speed_mph, bearing, altitude, manufacturer, status, telemetry, comment, location2d, location3d, raw, hash, receive_level, mark_level, space_level) values(%d, %d,NOW(), %d, %d, '%s','%s', '%s',%s, %s,%s,%s, %s, %s, %s, %s, %s, %s, %s, md5(%s), %d, %d, %d);\n", 
        DWInstance,
	    chan, 
        (ptr == NULL ? 0 : ptr->sdr),
        (ptr == NULL ? 0 : ptr->freq),
	    A->g_src, 
        heard, 
	    sname, 
        e_ssymbol,
	    sspd, 
        scse, 
        salt, 
	    e_smfr, 
        e_sstatus, 
        e_stelemetry, 
        e_scomment,
        geom_string, 
        geom_string3d, 
        e_packettext,
        e_infopart, 
        alevel.rec,
        alevel.mark,
        alevel.space);

    /* Execute the SQL statement */
    PGresult *res = PQexec(db_connection, sql_insert_string);
          
    
	/* Check return code */
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error inserting data into dw_packets table:  %s.\n", PQerrorMessage(db_connection));
	    dw_printf ("SQL:  %s.\n", sql_insert_string);

        text_color_set(DW_COLOR_INFO);
	    dw_printf ("Attempting without telemetry and comment fields.\n");

        // Try insert again with telemetry and comment fields set to NULL...
        PQclear(res);
           
        /* Create the SQL insert command... */
        snprintf (sql_insert_string, sizeof(sql_insert_string), "insert into dw_packets (instance, channel, tm, sdr, freq, callsign, heardfrom, sourcename, source_symbol, speed_mph, bearing, altitude, manufacturer, status, telemetry, comment, location2d, location3d, raw, hash, receive_level, mark_level, space_level) values(%d, %d,NOW(), %d, %d, '%s','%s', '%s',%s, %s,%s,%s, %s, %s, %s, %s, %s, %s, %s, md5(%s), %d, %d, %d);\n", 
            DWInstance,
            chan, 
            (ptr == NULL ? 0 : ptr->sdr),
            (ptr == NULL ? 0 : ptr->freq),
            A->g_src, 
            heard, 
            sname, 
            e_ssymbol,
            sspd, 
            scse, 
            salt, 
            e_smfr, 
            e_sstatus, 
            "NULL", 
            "NULL",
            geom_string, 
            geom_string3d, 
            e_packettext,
            e_infopart,
            alevel.rec,
            alevel.mark,
            alevel.space);

        /* re-execute the SQL command, but without the telemetry and comment fields. */
        res = PQexec(db_connection, sql_insert_string);

	    /* Check return code */
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
	        text_color_set(DW_COLOR_ERROR);
    	    dw_printf ("2nd attempt failed.  Error inserting data into dw_packets table:  %s.\n", PQerrorMessage(db_connection));
	        dw_printf ("SQL:  %s.\n", sql_insert_string);
        }

    }
       
	/* Free up mem... */
    PQfreemem(e_ssymbol);
    PQfreemem(e_smfr);
    PQfreemem(e_sstatus);
    PQfreemem(e_stelemetry);
    PQfreemem(e_scomment);
    PQfreemem(e_packettext);
    PQfreemem(e_infopart);

	/* Clear result */
    PQclear(res);

} /* end db_write_recv */




/*------------------------------------------------------------------
 *
 * Function:	db_term
 *
 * Purpose:	Close any open DB connections
 *		Called when exiting.
 *
 *------------------------------------------------------------------*/

void db_term (void) {
    PQfinish(db_connection);
	text_color_set(DW_COLOR_INFO);
	dw_printf("Closing DB connection.\n");
    
} /* end db_term */



/*------------------------------------------------------------------
 *
 * Function:    make_ascii_only
 *
 * Purpose:     This function is almost exactly identical to ax25_safe_print
 *              except that instead of actually printing the resulting 
 *              string, it returns it.
 *
 * Inputs:      pstr    - Pointer to string we want to convert.
 *              
 *              len     - Number of bytes.  If < 0 we use strlen().
 *
 *              safe_str    - destination buffer where contents of the output will be placed.
 *
 *              dest_size   - size of destination buffer
 *
 *              
 *              Stops after non-zero len characters or at nul.
 *
 * Returns:     returns the print "safe" string
 *
 * Description: convert a string in a "safe" printable string.
 *
 * ------------------------------------------------------------------*/

char * make_ascii_only (char *pstr, int len, char *safe_str, int dest_size) {
    int ch;
    int safe_len;

    safe_len = 0;
    safe_str[safe_len] = '\0';

    if (len < 0)
        len = strlen(pstr);

    if (len > MAX_SAFE)
        len = MAX_SAFE;

    while (len > 0) {
        ch = *((unsigned char *)pstr);
        if (ch == ' ' && (len == 1 || pstr[1] == '\0')) {
            snprintf (safe_str + safe_len, dest_size - safe_len, "<0x%02x>", ch);
            safe_len += 6;
        }
        else if (ch < ' ' || ch >= 0x80 ) {
            snprintf (safe_str + safe_len, dest_size - safe_len, "<0x%02x>", ch);
            safe_len += 6;
        }
        else {
            /* Everthing else is a printable char */
            safe_str[safe_len++] = ch;
            safe_str[safe_len] = '\0';
        }

        pstr++;
        len--;
    }

    return safe_str;

} /* end make_ascii_only  */


/*------------------------------------------------------------------
 * Trim any CR, LF from the end of line. 
 *
 *  ...borrowed from kissutil.c...
 * ------------------------------------------------------------------*/
void trim (char *stuff)
{
    char *p;
    p = stuff + strlen(stuff) - 1;
    while (strlen(stuff) > 0 && (*p == '\r' || *p == '\n')) {
      *p = '\0';
      p--;
    }
} /* end trim */



/* end db.c */
