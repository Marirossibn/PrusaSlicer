#ifndef slic3r_Utils_TCPConsole_hpp_
#define slic3r_Utils_TCPConsole_hpp_

#include <string>
#include <list>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>

namespace Slic3r {
    namespace Utils {

        using boost::asio::ip::tcp;

        class TCPConsole
        {
        public:
            TCPConsole();
            TCPConsole(const std::string& host_name, const std::string& port_name);
            ~TCPConsole() {}

            void set_defaults()
            {
                newline_ = "\n";
                done_string_ = "ok";
                connect_timeout_ = boost::chrono::milliseconds(5000);
                write_timeout_ = boost::chrono::milliseconds(10000);
                read_timeout_ = boost::chrono::milliseconds(10000);
            }

            void set_line_delimiter(const std::string& newline) {
                newline_ = newline;
            }
            void set_command_done_string(const std::string& done_string) {
                done_string_ = done_string;
            }

            void set_remote(const std::string& host_name, const std::string& port_name)
            {
                host_name_ = host_name;
                port_name_ = port_name;
            }

            bool enqueue_cmd(const std::string& cmd) {
                // TODO: Add multithread protection to queue
                cmd_queue_.push_back(cmd);
                return true;
            }

            bool run_queue();
            std::string error_message() {
                return error_code_.message();
            }

        private:
            void handle_connect(const boost::system::error_code& ec);
            void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred);
            void handle_write(const boost::system::error_code& ec, std::size_t bytes_transferred);

            void transmit_next_command();
            void wait_next_line();
            std::string extract_next_line();

            void set_deadline_in(boost::chrono::steady_clock::duration);
            bool is_deadline_over();

            std::string host_name_;
            std::string port_name_;
            std::string newline_;
            std::string done_string_;
            boost::chrono::steady_clock::duration connect_timeout_;
            boost::chrono::steady_clock::duration write_timeout_;
            boost::chrono::steady_clock::duration read_timeout_;

            std::list<std::string> cmd_queue_;

            boost::asio::io_context io_context_;
            tcp::resolver resolver_;
            tcp::socket socket_;
            boost::asio::streambuf recv_buffer_;
            std::string send_buffer_;

            bool is_connected_;
            boost::system::error_code error_code_;
            boost::chrono::steady_clock::time_point deadline_;
        };

    } // Utils
} // Slic3r

#endif
