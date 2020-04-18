#include "csv.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <csignal>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/scope_exit.hpp>
#include <boost/filesystem.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>
#include <boost/program_options.hpp>

namespace
{

    auto on_exit() -> std::vector<std::function<void(void)>>&
    {
        static std::vector<std::function<void(void)>> on_exit;
        return on_exit;
    }

    template <typename F>
    void add_exit_func(F f) {on_exit().emplace_back(f);}

    void signal_handler(int)
    {
        for (auto &&f : on_exit())
            std::invoke(f);
    }

}

struct vpn
{
    std::string ip;
    int score;
    int speed;
    std::string country;
    std::string config;
};

std::ostream& operator<<(std::ostream& os, vpn const& v)
{
    return os << "IP:      " << v.ip << "\n"
              << "Score:   " << v.score << "\n"
              << "Speed:   " << v.speed << "\n"
              << "Country: " << v.country << "\n";
}

auto get(std::string host, std::string port, std::string target)
    -> boost::beast::http::response<boost::beast::http::dynamic_body>
{
    namespace beast = boost::beast;     // from <boost/beast.hpp>
    namespace http = beast::http;       // from <boost/beast/http.hpp>
    namespace net = boost::asio;        // from <boost/asio.hpp>
    using tcp = net::ip::tcp;           // from <boost/asio/ip/tcp.hpp>

    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto const results = resolver.resolve(host, port);
    stream.connect(results);
    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;
    http::read(stream, buffer, res);

    // Gracefully close the socket
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    if(ec && ec != beast::errc::not_connected)
        throw beast::system_error{ec};

    return res;
}

std::string decode64(const std::string &val) {
    using namespace boost::archive::iterators;
    using It = transform_width<binary_from_base64<std::string::const_iterator>, 8, 6>;
    return boost::algorithm::trim_right_copy_if(std::string(It(std::begin(val)), It(std::end(val))), [](char c) {
        return c == '\0';
    });
}


int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    namespace fs = boost::filesystem;
    std::signal(SIGINT, signal_handler);
    try
    {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("country,c", po::value<std::string>()->default_value("JP"), "Country")
            ("list,l", "list available country")
            ("id", po::value<int>()->default_value(0), "the id on the vpn list")
            ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            std::cout << desc << '\n';
            std::exit(EXIT_SUCCESS);
        }

        std::vector<vpn> configs;
        {
            auto res = get("www.vpngate.net", "80", "/api/iphone/");
            std::string tmpfile =
                (fs::temp_directory_path() / fs::unique_path()).string();
            std::ofstream f {tmpfile};
            BOOST_SCOPE_EXIT(&tmpfile) {
                fs::remove(tmpfile);
            } BOOST_SCOPE_EXIT_END

            f << boost::beast::buffers_to_string(res.body().data()) << std::flush;
            csv::CSVReader rows(tmpfile);
            for (csv::CSVRow& row: rows)
                configs.emplace_back(vpn{
                    .ip      = row["IP"].get<std::string>(),
                    .score   = row["Score"].get<int>(),
                    .speed   = row["Speed"].get<int>(),
                    .country = row["CountryShort"].get<std::string>(),
                    .config  = row["OpenVPN_ConfigData_Base64"].get<std::string>()
                });
        }

        if (vm.count("list"))
        {
            std::vector<std::string> list;
            std::transform(configs.begin(), configs.end(), std::back_inserter(list),
                           [](vpn const & a){ return a.country; });
            std::sort(list.begin(), list.end());
            auto end = std::unique(list.begin(), list.end());
            for (auto it = list.begin(); it != end; ++it)
                std::cout << *it << "\n";
            std::exit(EXIT_SUCCESS);
        }

        auto back = std::stable_partition(configs.begin(), configs.end(),
                              [country = vm["country"].as<std::string>()](vpn const & a) {
                                  return a.country == country;
                              });
        std::sort(configs.begin(), back, [](vpn const & a, vpn const & b) {
                                             return a.score > b.score;
                                         });
        std::string configfile =
            (fs::temp_directory_path() / fs::unique_path()).string();

        add_exit_func([configfile] { fs::remove(configfile); });
        BOOST_SCOPE_EXIT(&configfile) { fs::remove(configfile); } BOOST_SCOPE_EXIT_END
        {
            std::ofstream f {configfile};
            std::cout << configs.at(vm["id"].as<int>()) << "\n";
            f << decode64(configs.at(vm["id"].as<int>()).config) << std::flush;
        }
        {
            namespace bp = boost::process;
            bp::child c(bp::search_path("openvpn"), configfile, bp::std_out > stdout);
            c.wait();
        }
    }
    catch(std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
