#include <exception>
#include <sstream>
#include <iostream>

#include "Zipper.hpp"
#include "miniz/miniz_zip.h"
#include <boost/filesystem/path.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

class Zipper::Impl {
public:
    mz_zip_archive arch;

    std::string get_errorstr(mz_zip_error mz_err)
    {
        switch (mz_err)
        {
            case MZ_ZIP_NO_ERROR:
                return "no error";
            case MZ_ZIP_UNDEFINED_ERROR:
                return L("undefined error");
            case MZ_ZIP_TOO_MANY_FILES:
                return L("too many files");
            case MZ_ZIP_FILE_TOO_LARGE:
                return L("file too large");
            case MZ_ZIP_UNSUPPORTED_METHOD:
                return L("unsupported method");
            case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
                return L("unsupported encryption");
            case MZ_ZIP_UNSUPPORTED_FEATURE:
                return L("unsupported feature");
            case MZ_ZIP_FAILED_FINDING_CENTRAL_DIR:
                return L("failed finding central directory");
            case MZ_ZIP_NOT_AN_ARCHIVE:
                return L("not a ZIP archive");
            case MZ_ZIP_INVALID_HEADER_OR_CORRUPTED:
                return L("invalid header or archive is corrupted");
            case MZ_ZIP_UNSUPPORTED_MULTIDISK:
                return L("unsupported multidisk archive");
            case MZ_ZIP_DECOMPRESSION_FAILED:
                return L("decompression failed or archive is corrupted");
            case MZ_ZIP_COMPRESSION_FAILED:
                return L("compression failed");
            case MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE:
                return L("unexpected decompressed size");
            case MZ_ZIP_CRC_CHECK_FAILED:
                return L("CRC-32 check failed");
            case MZ_ZIP_UNSUPPORTED_CDIR_SIZE:
                return L("unsupported central directory size");
            case MZ_ZIP_ALLOC_FAILED:
                return L("allocation failed");
            case MZ_ZIP_FILE_OPEN_FAILED:
                return L("file open failed");
            case MZ_ZIP_FILE_CREATE_FAILED:
                return L("file create failed");
            case MZ_ZIP_FILE_WRITE_FAILED:
                return L("file write failed");
            case MZ_ZIP_FILE_READ_FAILED:
                return L("file read failed");
            case MZ_ZIP_FILE_CLOSE_FAILED:
                return L("file close failed");
            case MZ_ZIP_FILE_SEEK_FAILED:
                return L("file seek failed");
            case MZ_ZIP_FILE_STAT_FAILED:
                return L("file stat failed");
            case MZ_ZIP_INVALID_PARAMETER:
                return L("invalid parameter");
            case MZ_ZIP_INVALID_FILENAME:
                return L("invalid filename");
            case MZ_ZIP_BUF_TOO_SMALL:
                return L("buffer too small");
            case MZ_ZIP_INTERNAL_ERROR:
                return L("internal error");
            case MZ_ZIP_FILE_NOT_FOUND:
                return L("file not found");
            case MZ_ZIP_ARCHIVE_TOO_LARGE:
                return L("archive is too large");
            case MZ_ZIP_VALIDATION_FAILED:
                return L("validation failed");
            case MZ_ZIP_WRITE_CALLBACK_FAILED:
                return L("write calledback failed");
            default:
                break;
        }

        return "unknown error";
    }

    [[noreturn]] void blow_up() {
        throw std::runtime_error(get_errorstr(arch.m_last_error));
    }
};

Zipper::Zipper(const std::string &zipfname, e_compression compression)
{
    m_impl.reset(new Impl());

    memset(&m_impl->arch, 0, sizeof(m_impl->arch));

    // Initialize the archive data
    if(!mz_zip_writer_init_file(&m_impl->arch, zipfname.c_str(), 0))
        m_impl->blow_up();

    m_compression = compression;
    m_zipname = zipfname;
}

Zipper::~Zipper()
{
    finish_entry();

    if(!mz_zip_writer_finalize_archive(&m_impl->arch)) m_impl->blow_up();
    if(!mz_zip_writer_end(&m_impl->arch)) m_impl->blow_up();
}

Zipper::Zipper(Zipper &&m):
    m_impl(std::move(m.m_impl)),
    m_data(std::move(m.m_data)),
    m_entry(std::move(m.m_entry)),
    m_compression(m.m_compression),
    m_zipname(m.m_zipname) {}

Zipper &Zipper::operator=(Zipper &&m) {
    m_impl = std::move(m.m_impl);
    m_data = std::move(m.m_data);
    m_entry = std::move(m.m_entry);
    m_compression = m.m_compression;
    m_zipname = std::move(m.m_zipname);
    return *this;
}

void Zipper::add_entry(const std::string &name)
{
    finish_entry(); // finish previous business
    m_entry = name;
}

void Zipper::finish_entry()
{
    if(!m_data.empty() > 0 && !m_entry.empty()) {
        mz_uint compression = MZ_NO_COMPRESSION;

        switch (m_compression) {
        case NO_COMPRESSION: compression = MZ_NO_COMPRESSION; break;
        case FAST_COMPRESSION: compression = MZ_BEST_SPEED; break;
        case TIGHT_COMPRESSION: compression = MZ_BEST_COMPRESSION; break;
        }

        if(!mz_zip_writer_add_mem(&m_impl->arch, m_entry.c_str(),
                                  m_data.c_str(),
                                  m_data.size(),
                                  compression)) m_impl->blow_up();
    }

    m_data.clear();
    m_entry.clear();
}

std::string Zipper::get_name() const {
    return boost::filesystem::path(m_zipname).stem().string();
}

}
