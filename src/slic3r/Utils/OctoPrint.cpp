#include "OctoPrint.hpp"

#include <algorithm>
#include <sstream>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <wx/progdlg.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "Http.hpp"


namespace fs = boost::filesystem;
namespace pt = boost::property_tree;


namespace Slic3r {

OctoPrint::OctoPrint(DynamicPrintConfig *config) :
    host(config->opt_string("print_host")),
    apikey(config->opt_string("printhost_apikey")),
    cafile(config->opt_string("printhost_cafile"))
{}

OctoPrint::~OctoPrint() {}

bool OctoPrint::test(wxString &msg) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    bool res = true;
    auto url = make_url("api/version");

    BOOST_LOG_TRIVIAL(info) << boost::format("Octoprint: Get version at: %1%") % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Octoprint: Error getting version: %1%, HTTP %2%, body: `%3%`") % error % status % body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&, this](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("Octoprint: Got version: %1%") % body;

            try {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                if (! ptree.get_optional<std::string>("api")) {
                    res = false;
                    return;
                }

                const auto text = ptree.get_optional<std::string>("text");
                res = validate_version_text(text);
                if (! res) {
                    msg = wxString::Format(_(L("Mismatched type of print host: %s")), text ? *text : "OctoPrint");
                }
            }
            catch (...) {
                res = false;
                msg = "Could not parse server response";
            }
        })
        .perform_sync();

    return res;
}

wxString OctoPrint::get_test_ok_msg () const
{
    return _(L("Connection to OctoPrint works correctly."));
}

wxString OctoPrint::get_test_failed_msg (wxString &msg) const
{
    return wxString::Format("%s: %s\n\n%s",
        _(L("Could not connect to OctoPrint")), msg, _(L("Note: OctoPrint version at least 1.1.0 is required.")));
}

bool OctoPrint::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const
{
    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();

    wxString test_msg;
    if (! test(test_msg)) {
        error_fn(std::move(test_msg));
        return false;
    }

    bool res = true;

    auto url = make_url("api/files/local");

    BOOST_LOG_TRIVIAL(info) << boost::format("Octoprint: Uploading file %1% at %2%, filename: %3%, path: %4%, print: %5%")
        % upload_data.source_path.string()
        % url
        % upload_filename.string()
        % upload_parent_path.string()
        % upload_data.start_print;

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.form_add("print", upload_data.start_print ? "true" : "false")
        .form_add("path", upload_parent_path.string())      // XXX: slashes on windows ???
        .form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("Octoprint: File uploaded: HTTP %1%: %2%") % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Octoprint: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
            // error_fn(std::move(body), std::move(error), status);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            prorgess_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(error) << "Octoprint: Upload canceled";
                res = false;
            }
        })
        .perform_sync();

    return res;
}

bool OctoPrint::has_auto_discovery() const
{
    return true;
}

bool OctoPrint::can_test() const
{
    return true;
}

bool OctoPrint::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "OctoPrint") : true;
}

void OctoPrint::set_auth(Http &http) const
{
    http.header("X-Api-Key", apikey);

    if (! cafile.empty()) {
        http.ca_file(cafile);
    }
}

std::string OctoPrint::make_url(const std::string &path) const
{
    if (host.find("http://") == 0 || host.find("https://") == 0) {
        if (host.back() == '/') {
            return (boost::format("%1%%2%") % host % path).str();
        } else {
            return (boost::format("%1%/%2%") % host % path).str();
        }
    } else {
        return (boost::format("http://%1%/%2%") % host % path).str();
    }
}

wxString OctoPrint::format_error(const std::string &body, const std::string &error, unsigned status)
{
    if (status != 0) {
        auto wxbody = wxString::FromUTF8(body.data());
        return wxString::Format("HTTP %u: %s", status, wxbody);
    } else {
        return wxString::FromUTF8(error.data());
    }
}


// SLAHost

SLAHost::~SLAHost() {}

wxString SLAHost::get_test_ok_msg () const
{
    return _(L("Connection to Prusa SLA works correctly."));
}

wxString SLAHost::get_test_failed_msg (wxString &msg) const
{
    return wxString::Format("%s: %s", _(L("Could not connect to Prusa SLA")), msg);
}

bool SLAHost::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "Prusa SLA") : false;
}


}
