/*
 * Modify MP4 created date/time metadata
 * $Id$
 *
 * Google Photos appears to use the ftyp > moov > mvhd > "create date / time" for sorting the videos
 * This utility can be used to show or set this time stamp.
 *
 * Platform: win32 console app.  Mileage may vary on other platforms.
 *
 * MP4 atom documentation can be found at
 * http://atomicparsley.sourceforge.net/mpeg-4files.html
 * https://developer.apple.com/library/mac/documentation/QuickTime/QTFF/QTFFChap2/qtff2.html#//apple_ref/doc/uid/TP40000939-CH204-SW1
 *
 * (c) Copyright 2015 by Coert Vonk - coertvonk.com
 * MIT License
 * All rights reserved.  Use of copyright notice does not imply publication.
 */

#include "stdafx.h"
#include <stdint.h>
#include <inttypes.h>
#include <fstream>
#include <iostream>
#include "mp4stream.h"

uint_least8_t const NAME_LEN = 4;

struct hdr_t {
    uint64_t len;
    char name[NAME_LEN + 1];
};

bool readHdr( mp4Stream * const stream, hdr_t * const hdr )
{
    uint32_t len = stream->readUint32();  // read 32-bit 
    if ( len == 1 ) { // extended size
        hdr->len = stream->readUint64() - sizeof(uint32_t) - sizeof(uint64_t);
    } else {
        hdr->len = len - sizeof( uint32_t );
    }

    if ( hdr->len < 4 ) {
        return false;
    }

    stream->read( (char *)hdr->name, NAME_LEN );
    hdr->name[NAME_LEN] = '\0';
    hdr->len -= NAME_LEN;
    return true;
}

enum class state_t {
    findFtyp,
    findMoov,
    findMvhd,
    found
};

using namespace std;

#include <time.h>

uint64_t const diff = 2082844800UL;


inline uint64_t
time1904to1970( uint64_t const t )
{
    return t - diff;
}

inline uint64_t
time1970to1904( uint64_t const t )
{
    return t + diff;
}


void
printTime1904( uint64_t const t1904 )  // time in sec since 1904
{
    tm tm;
    time_t const t1970 = time1904to1970( t1904 );
    localtime_s( &tm, &t1970 );
    printf( "%04u-%02u-%02uT%02u:%02u:%02u\n", 1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec );
}

uint64_t  // return 0 on error
string2time1904( char * s )
{
    tm tm = {};
    uint32_t years;
    uint32_t months;

    if ( 6 == sscanf_s( s, "%04u-%02u-%02uT%02u:%02u:%02u", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec ) ||
         6 == sscanf_s( s, "%04u%02u%02uT%02u%02u%02u", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec ) ) {

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1; // unknown

    } else {
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;

        if ( 3 == sscanf_s( s, "%04u-%02u-%02u", &tm.tm_year, &tm.tm_mon, &tm.tm_mday ) ||
             3 == sscanf_s( s, "%04u%02u%02u", &tm.tm_year, &tm.tm_mon, &tm.tm_mday ) ) {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            tm.tm_isdst = -1; // unknown

        } else if ( 2 == sscanf_s( s, "Johan (%" PRIu32 "j%" PRIu32 "m)", &years, &months ) ) {
            months += years * 12;
            months += (2003 - 1900 ) * 12 + 11;
            tm.tm_year = months / 12;
            tm.tm_mon = months % 12;
            tm.tm_mday = 1;
            tm.tm_isdst = -1;
        } else {
            return 0;
        }
    }

    time_t n1970 = mktime( &tm );
    return time1970to1904( n1970 );
}


char * 
getCmdOption( char * * begin, char * * end, const std::string & option )
{
    char * * itr = std::find( begin, end, option );
    if ( itr != end && ++itr != end ) {
        return *itr;
    }
    return NULL;
}

bool 
cmdOptionExists( char * * begin, char * * end, const std::string & option )
{
    return std::find( begin, end, option ) != end;
}


int main(int argc, char * argv[])
{
    bool const dryrun = cmdOptionExists( argv, argv + argc, "--dry-run" );
    char * fname = getCmdOption( argv, argv + argc, "--file" );
    char * date = getCmdOption( argv, argv + argc, "--create" );

    if ( !fname ) {
        printf( "Usage: %s [--create isodate] [--dry-run] --file fname\n", argv[0] );
        printf( "  where isodate e.g. 2015-06-19T21:39 or 2015-06-19\n");
        return -1;
    }

    uint64_t date1904 = 0;
    if ( date ) {
        date1904 = string2time1904( date );
        if ( !date1904 ) {
            cout << "error parsing date(" << date << ")\n";
            return -1;
        }
    }

    mp4Stream * stream = new mp4Stream( fname );
    state_t state = state_t::findFtyp;

    while ( !stream->eof() && state != state_t::found ) {

        hdr_t hdr;
        if ( readHdr( stream, &hdr ) == false ) {
            return -3;
        }

        switch ( state ) {
            case state_t::findFtyp:  // first atom is always "ftyp", type type compatibility
                if ( strcmp( hdr.name, "ftyp" ) == 0 ) {
                    stream->seekp( hdr.len, ios::cur );
                    //stream->ignore( hdr.len );
                    state = state_t::findMoov;
                } else {
                    stream->seekp( hdr.len, ios::cur );
                    //stream->ignore( hdr.len );
                }
                break;
            case state_t::findMoov:  // second atom is "moov" container
                if ( strcmp( hdr.name, "moov" ) == 0 ) {
                    state = state_t::findMvhd;
                } else {
                    stream->seekp( hdr.len, ios::cur );
                    //stream->ignore( hdr.len );
                }
                break;
            case state_t::findMvhd:  // third atom is "mvhd" (inside the "moov" container)
                if ( strcmp( hdr.name, "mvhd" ) == 0 ) {
                    state = state_t::found;
                } else {
                    stream->seekp( hdr.len, ios::cur );
                    //stream->ignore( hdr.len );
                }
                break;
            case state_t::found:  // just to please the compiler
                break;
        }
    }

    if ( stream->eof() ) {
        cout << "eof\n";
        return -4;
    }

    uint8_t const version = stream->readUint8();  // first byte is version nr
    stream->ignore( 3 );  // next three are reserved

    // create date/time is stored here
    if ( date ) {
        if ( dryrun ) {
            printTime1904( date1904 );
        } else {
            if ( version == 1 ) {
                stream->writeUint64( date1904 );
            } else {
                (void)stream->readUint32();
                stream->seekp( -4, ios::cur );
                stream->writeUint32( (uint32_t)date1904 );
            }
        }
    } else {
        uint64_t creationTime = (version == 1)
            ? stream->readUint64()
            : stream->readUint32();
        printTime1904( creationTime );
    }

    delete stream;
    return EXIT_SUCCESS;
}

