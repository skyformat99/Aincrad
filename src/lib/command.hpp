#ifndef __COMMAND__
#define __COMMAND__

#include <boost/any.hpp>
#include <boost/filesystem.hpp>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <stack>
#include <string>
#include <vector>

#include "client.hpp"
#include "config.h"
#include "package.hpp"
#include "server.hpp"
#include "util.h"

using std::vector;
using std::string;
using std::cout;
using std::endl;

namespace fs = boost::filesystem;

static std::string script_dir( "" );

class Operate {
   public:
    struct wrapped {
        wrapped( std::deque<std::string> _astack,
                 std::deque<std::string> _vstack, network::package_ptr _package,
                 network::session_ptr _session, network::server_ptr _server,
                 network::client_ptr _client )
            : astack( _astack ),
              vstack( _vstack ),
              package( _package ),
              session( _session ),
              server( _server ),
              client( _client ){};

        std::deque<std::string> astack;
        std::deque<std::string> vstack;
        network::package_ptr    package;
        network::session_ptr    session;
        network::server_ptr     server;
        network::client_ptr     client;
    };

    static void next( wrapped& w ) {
        while ( !w.astack.empty() ) {
            if ( fn_map.find( w.astack.back() ) == fn_map.end() ) {
                w.vstack.push_back( w.astack.back() );
                w.astack.pop_back();
            } else {
                auto command = w.astack.back();
                w.astack.pop_back();
                fn_map[command]( w );
                break;
            }
        }
    };

    static void _const( std::vector<std::string>& argv, network::package_ptr,
                        network::session_ptr, network::server_ptr,
                        network::client_ptr client ) {
        int level = 0;
        for ( auto it = argv.begin(); it != argv.end(); ++it ) {
            if ( *it == "[" ) {
                if ( level == 0 ) it = argv.erase( it );
                ++level;
            } else if ( *it == "]" ) {
                --level;
                if ( level == 0 ) it = argv.erase( it );
            } else if ( *it == "this" && !level ) {
                *it = client->hostname();
            }
            if ( it == argv.end() ) break;
        }
    }

    static void process( std::string line, network::package_ptr package,
                         network::session_ptr session,
                         network::server_ptr  server,
                         network::client_ptr  client ) {
        auto argv = util::split( line, '$' );
        _const( argv, package, session, server, client );
        wrapped w( std::deque<std::string>(), std::deque<std::string>(),
                   package, session, server, client );
        for ( auto arg : argv ) {
            w.astack.push_back( arg );
        }
        next( w );
    };

    static std::string _pack( wrapped& w ) {
        auto vp = std::accumulate(
            w.vstack.begin(), w.vstack.end(), string( "" ),
            [&]( const string& s1, const string& s2 ) -> string {
                return s1.empty() ? s2 : s2 + "$" + s1;
            } );
        auto ap = std::accumulate(
            w.astack.begin(), w.astack.end(), string( "" ),
            [&]( const string& s1, const string& s2 ) -> string {
                return s1.empty() ? s2 : s1 + "$" + s2;
            } );
        return ap + ( vp == "" ? "" : "$" ) + vp;
    }

    static void dup( wrapped& w ) {
        w.vstack.push_back( w.vstack.back() );
        next( w );
    }

    static void swap( wrapped& w ) {
        auto a = w.vstack.back();
        w.vstack.pop_back();
        auto b = w.vstack.back();
        w.vstack.pop_back();
        w.vstack.push_back( a );
        w.vstack.push_back( b );
        next( w );
    }

    static void size( wrapped& w ) {
        w.vstack.push_back( std::to_string( w.vstack.size() ) );
        next( w );
    }

    static void print( wrapped& w ) {
        std::cout << std::accumulate(
                         w.vstack.begin(), w.vstack.end(), string( "" ),
                         []( const string& s1, const string& s2 ) -> string {
                             return s1.empty() ? s2 : s1 + " " + s2;
                         } )
                  << std::endl;
        next( w );
    }

    static void print_limit( wrapped& w ) {
        auto n = std::atoi( w.vstack.back().c_str() );
        w.vstack.pop_back();
        std::string p;
        for ( auto it = w.vstack.rbegin(); it != w.vstack.rend() && n > 0;
              ++it, --n ) {
            p = *it + " " + p;
        }
        std::cout << p << std::endl;
        next( w );
    }

    static void drop( wrapped& w ) {
        auto n = std::atoi( w.vstack.back().c_str() );
        w.vstack.pop_back();
        while ( n-- > 0 ) {
            w.vstack.pop_back();
        }
        next( w );
    }

    static void drop_one( wrapped& w ) {
        w.vstack.push_back( "1" );
        drop( w );
    }

    static void _if( wrapped& w ) {
        bool                    _else_part = false;
        std::deque<std::string> t_deque;
        std::deque<std::string> f_deque;

        while ( w.astack.back() != "then" ) {
            if ( w.astack.back() == "else" ) {
                _else_part = true;
            } else if ( _else_part ) {
                f_deque.push_back( w.astack.back() );
            } else {
                t_deque.push_back( w.astack.back() );
            }
            w.astack.pop_back();
        }
        w.astack.pop_back();

        auto cond = w.vstack.back();
        w.vstack.pop_back();
        if ( cond == "0" ) {
            // if false
            while ( !f_deque.empty() ) {
                w.astack.push_back( f_deque.back() );
                f_deque.pop_back();
            }
        } else {
            while ( !t_deque.empty() ) {
                w.astack.push_back( t_deque.back() );
                t_deque.pop_back();
            }
        }

        next( w );
    }

    static void begin( wrapped& w ) {
        std::deque<std::string> inner;

        auto it = w.astack.rbegin();

        while ( *it != "end" ) {
            inner.push_back( *it );
            ++it;
        }

        w.astack.push_back( "begin" );

        while ( !inner.empty() ) {
            w.astack.push_back( inner.back() );
            inner.pop_back();
        }

        next( w );
    }

    static void exit( wrapped& w ) {
        while ( !w.astack.empty() && w.astack.back() != "end" ) {
            w.astack.pop_back();
        }
        if ( !w.astack.empty() && w.astack.back() == "end" ) {
            w.astack.pop_back();
        }

        next( w );
    }

    static void to( wrapped& w ) {
        auto hostname = w.vstack.back();
        w.vstack.pop_back();
        auto p = _pack( w );
        w.server->sent_to( std::make_shared<network::Package>( p ), hostname );
    }

    static void broadcast( wrapped& w ) {
        auto block = w.vstack.back();
        w.vstack.pop_back();
        auto p = _pack( w );
        w.server->broadcast( std::make_shared<network::Package>( p ),
                             [&]( network::session_ptr session ) {
                                 return block != session->hostname;
                             } );
    }

    static void time( wrapped& w ) {
        w.vstack.push_back( util::get_time() );
        next( w );
    }

    static void minus( wrapped& w ) {
        long a = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        long b = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        w.vstack.push_back( std::to_string( b - a ) );
        next( w );
    }

    static void add( wrapped& w ) {
        long a = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        long b = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        w.vstack.push_back( std::to_string( a + b ) );
        next( w );
    }

    static void greater( wrapped& w ) {
        long a = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        long b = std::stol( w.vstack.back() );
        w.vstack.pop_back();
        w.vstack.push_back( b > a ? "1" : "0" );
        next( w );
    }

    static void equal( wrapped& w ) {
        auto a = w.vstack.back();
        w.vstack.pop_back();
        auto b = w.vstack.back();
        w.vstack.pop_back();
        w.vstack.push_back( b == a ? "1" : "0" );
        next( w );
    }

    static void sadd( wrapped& w ) {
        auto a = w.vstack.back();
        w.vstack.pop_back();
        auto b = w.vstack.back();
        w.vstack.pop_back();
        w.vstack.push_back( a + b );
        next( w );
    }

    static void list_host( wrapped& w ) {
        if ( w.server == nullptr ) return;
        w.vstack.push_back( std::accumulate(
            w.server->get_clients().begin(), w.server->get_clients().end(),
            string( "" ),
            []( const string& s1, network::session_ptr s2 ) -> string {
                return s1.empty()
                           ? "[" + s2->get_client_s() + "] " + s2->hostname
                           : s1 + "\n[" + s2->get_client_s() + "] " +
                                 s2->hostname;
            } ) );
        next( w );
    }

    static void push_host( wrapped& w ) {
        if ( w.server == nullptr ) return;
        for ( auto it = w.server->get_clients().begin();
              it != w.server->get_clients().end(); ++it ) {
            w.vstack.push_back( ( *it )->hostname );
        }
        next( w );
    }

    static void reg( wrapped& w ) {
        w.session->hostname = w.vstack.back();
        w.vstack.pop_back();
        next( w );
    }

    static void forward( wrapped& w ) {
        auto p = _pack( w );
        if ( w.client )
            w.client->send( std::make_shared<network::Package>( p ) );
    }

    static void system( wrapped& w ) {
        auto command = w.vstack.back();
        w.vstack.pop_back();

        auto output = util::exec( command.c_str(), false );
        w.vstack.push_back( output );

        next( w );
    }

    static void set_hostname( wrapped& w ) {
        w.client->set_hostname( w.vstack.back() );
        w.vstack.pop_back();
        w.client->send( std::make_shared<network::Package>(
            "reg$" + w.client->hostname() ) );
    }

    static void run( wrapped& w ) {
        auto filename = w.vstack.back();
        w.vstack.pop_back();

        auto path = fs::path( script_dir + filename );

        if ( !fs::exists( path ) ) {
            return;
        }

        fs::ifstream file( path );
        string       str;
        string       command;

        std::map<string, string> var;

        while ( getline( file, str ) ) {
            if ( str.length() == 0 ) {
                if ( !command.empty() ) {
                    Operate::process( command, w.package, w.session, w.server,
                                      w.client );
                }
                command = "";
                continue;
            }
            if ( str[0] == '#' ) {
                var[str.substr( 1 )] = w.vstack.back();
                w.vstack.pop_back();
                continue;
            }
            if ( var.find( str ) != var.end() ) {
                str = var[str];
            }
            command = command + ( command.empty() ? "" : "$" ) + str;
        }

        next( w );
    }

    static void s_list_host( wrapped& w ) {
        w.astack.clear();
        w.vstack.clear();
        w.astack.push_back( "print" );
        w.astack.push_back( "->" );
        w.astack.push_back( w.client->hostname() );
        w.astack.push_back( "list_host" );
        w.astack.push_back( "->>" );
        next( w );
    }

    static void s_ping( wrapped& w ) {
        auto client_hostname = w.vstack.back();
        w.vstack.clear();
        w.astack.clear();
        w.astack.push_back( "print" );
        w.astack.push_back( "connected" );
        w.astack.push_back( "[" );
        w.astack.push_back( "this" );
        w.astack.push_back( "]" );
        w.astack.push_back( "ns" );
        w.astack.push_back( "-" );
        w.astack.push_back( "time" );
        w.astack.push_back( "->" );
        w.astack.push_back( w.client->hostname() );
        w.astack.push_back( "->>" );
        w.astack.push_back( "->" );
        w.astack.push_back( client_hostname );
        w.astack.push_back( "->>" );
        w.astack.push_back( "time" );
        next( w );
    }

    static void sft( wrapped& w ) {
        auto filename = w.vstack.back();
        w.vstack.pop_back();

        try {
            if ( !fs::exists( fs::path( filename ) ) ) {
                return;
            }
            auto file =
                std::make_shared<boost::iostreams::mapped_file_source>();
            if ( fs::file_size( fs::path( filename ) ) > 0 ) {
                file->open( filename );
            }
            if ( w.server ) {
                auto hostname = w.vstack.back();
                w.vstack.pop_back();
                w.server->sent_to( std::make_shared<network::Package>( file ),
                                   hostname );
            } else {
                w.client->send( std::make_shared<network::Package>( file ) );
            }
            if ( file->is_open() ) file->close();
        } catch ( const std::exception& ex ) {
            std::cout << ex.what() << std::endl;
        }

        next( w );
    }

    static void popfs( wrapped& w ) {
        fs::path full_path( fs::initial_path<fs::path>() );
        full_path = fs::system_complete( fs::path( TEMP_PATH ) );
        if ( !fs::exists( full_path ) ) {
            fs::create_directory( full_path );
        }
        fs::directory_iterator end_iter;
        size_t                 file_count = 0;
        for ( fs::directory_iterator dir_itr( full_path ); dir_itr != end_iter;
              ++dir_itr ) {
            if ( fs::is_regular_file( dir_itr->status() ) ) {
                if ( util::is_number( dir_itr->path().filename().string() ) ) {
                    ++file_count;
                }
            }
        }

        --file_count;

        auto filename = w.vstack.back();
        w.vstack.pop_back();

        try {
            fs::path target( fs::system_complete( fs::path( filename ) ) );
            fs::path accu( "/" );
            fs::path::iterator it( target.begin() ), it_end( target.end() );
            ++it;
            --it_end;
            for ( ; it != it_end; ++it ) {
                accu /= it->filename().string();
                if ( !fs::exists( accu ) ) {
                    fs::create_directory( accu );
                }
            }

            fs::rename(
                fs::system_complete( fs::path( "./" + TEMP_PATH + "/" +
                                               std::to_string( file_count ) ) ),
                fs::system_complete( fs::path( filename ) ) );
        } catch ( const std::exception& ex ) {
            std::cout << ex.what() << std::endl;
        }

        next( w );
    }

    static void tree( wrapped& w ) {
        auto dir = w.vstack.back();
        w.vstack.pop_back();

        fs::path full_path( fs::initial_path<fs::path>() );
        full_path = fs::system_complete( fs::path( dir ) );
        if ( !fs::exists( full_path ) ) {
            return;
        }

        fs::recursive_directory_iterator end_iter;
        for ( fs::recursive_directory_iterator dir_itr( full_path );
              dir_itr != end_iter; ++dir_itr ) {
            if ( fs::is_regular_file( dir_itr->status() ) ) {
                w.vstack.push_back(
                    fs::relative( dir_itr->path(), full_path ).string() );
            }
        }

        next( w );
    }

   private:
    typedef std::map<std::string, std::function<void( Operate::wrapped& )>>
                 FnMap;
    static FnMap fn_map;
};

Operate::FnMap Operate::fn_map = {{"dup", &Operate::dup},
                                  {"swap", &Operate::swap},
                                  {"size", &Operate::size},
                                  {"print", &Operate::print},
                                  {"print_limit", &Operate::print_limit},
                                  {"drop_one", &Operate::drop_one},
                                  {"drop", &Operate::drop},
                                  // archimatic operation
                                  {"-", &Operate::minus},
                                  {"+", &Operate::add},
                                  {">", &Operate::greater},
                                  {"==", &Operate::equal},
                                  {"++", &Operate::sadd},
                                  // cond operation
                                  {"if", &Operate::_if},
                                  {"begin", &Operate::begin},
                                  {"exit", &Operate::exit},
                                  // network operation
                                  {"->>", &Operate::forward},
                                  {"forward", &Operate::forward},
                                  {"reg", &Operate::reg},
                                  {"->", &Operate::to},
                                  {"to", &Operate::to},
                                  {"system", &Operate::system},
                                  {"time", &Operate::time},
                                  {"broadcast", &Operate::broadcast},
                                  {"set_hostname", &Operate::set_hostname},
                                  {"list_host", &Operate::list_host},
                                  {"push_host", &Operate::push_host},
                                  {"run", &Operate::run},
                                  // file stack operation
                                  {"tree", &Operate::tree},
                                  {"sft", &Operate::sft},
                                  {"sf", &Operate::sft},
                                  {"sendfile", &Operate::sft},
                                  {"popfs", &Operate::popfs},
                                  // sugar
                                  {"@list_host", &Operate::s_list_host},
                                  //{"@system", &Operate::s_system},
                                  {"@ping", &Operate::s_ping}};

// register command processor
void register_processor( network::server_ptr server,
                         network::client_ptr client ) {
    if ( script_dir == "" ) {
        util::config _conf_remote;
        _conf_remote.read_config( util::get_working_path() + "/.config" );
        script_dir = _conf_remote.value( "script", "dir" );
    }

    if ( server )
        server->on( "recv_package", []( network::package_ptr package,
                                        network::session_ptr session,
                                        network::server_ptr  server ) {
            Operate::process( string( package->body(), package->body_length() ),
                              package, session, server, NULL );
        } );

    if ( client )
        client->on( "recv_package", []( network::package_ptr package,
                                        network::client_ptr client ) {
            Operate::process( string( package->body(), package->body_length() ),
                              package, NULL, NULL, client );
        } );
    return;
}

#endif
