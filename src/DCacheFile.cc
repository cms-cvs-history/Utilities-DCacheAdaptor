#include "Utilities/DCacheAdaptor/interface/DCacheFile.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <unistd.h>
#include <fcntl.h>
#include <dcap.h>

DCacheFile::DCacheFile (void)
  : m_fd (EDM_IOFD_INVALID),
    m_close (false)
{}

DCacheFile::DCacheFile (IOFD fd)
  : m_fd (fd),
    m_close (true)
{}

DCacheFile::DCacheFile (const char *name,
    	    int flags /* = IOFlags::OpenRead */,
    	    int perms /* = 066 */)
  : m_fd (EDM_IOFD_INVALID),
    m_close (false)
{ open (name, flags, perms); }

DCacheFile::DCacheFile (const std::string &name,
    	    int flags /* = IOFlags::OpenRead */,
    	    int perms /* = 066 */)
  : m_fd (EDM_IOFD_INVALID),
    m_close (false)
{ open (name.c_str (), flags, perms); }

DCacheFile::~DCacheFile (void)
{
  if (m_close)
    edm::LogError("DCacheFileError")
      << "Destructor called on dCache file '" << m_name
      << "' but the file is still open";
}

//////////////////////////////////////////////////////////////////////
void
DCacheFile::create (const char *name,
		    bool exclusive /* = false */,
		    int perms /* = 066 */)
{
  open (name,
        (IOFlags::OpenCreate | IOFlags::OpenWrite | IOFlags::OpenTruncate
         | (exclusive ? IOFlags::OpenExclusive : 0)),
        perms);
}

void
DCacheFile::create (const std::string &name,
                    bool exclusive /* = false */,
                    int perms /* = 066 */)
{
  open (name.c_str (),
        (IOFlags::OpenCreate | IOFlags::OpenWrite | IOFlags::OpenTruncate
         | (exclusive ? IOFlags::OpenExclusive : 0)),
        perms);
}

void
DCacheFile::open (const std::string &name,
                  int flags /* = IOFlags::OpenRead */,
                  int perms /* = 066 */)
{ open (name.c_str (), flags, perms); }

void
DCacheFile::open (const char *name,
                  int flags /* = IOFlags::OpenRead */,
                  int perms /* = 066 */)
{
  m_name = name;

  // Actual open
  if ((name == 0) || (*name == 0))
    throw cms::Exception("DCacheFile::open()")
      << "Cannot open a file without a name";

  if ((flags & (IOFlags::OpenRead | IOFlags::OpenWrite)) == 0)
    throw cms::Exception("DCacheFile::open()")
      << "Must open file '" << name << "' at least for read or write";

  // If I am already open, close old file first
  if (m_fd != EDM_IOFD_INVALID && m_close)
    close ();

  // Translate our flags to system flags
  int openflags = 0;

  if ((flags & IOFlags::OpenRead) && (flags & IOFlags::OpenWrite))
    openflags |= O_RDWR;
  else if (flags & IOFlags::OpenRead)
    openflags |= O_RDONLY;
  else if (flags & IOFlags::OpenWrite)
    openflags |= O_WRONLY;

  if (flags & IOFlags::OpenNonBlock)
    openflags |= O_NONBLOCK;

  if (flags & IOFlags::OpenAppend)
    openflags |= O_APPEND;

  if (flags & IOFlags::OpenCreate)
    openflags |= O_CREAT;

  if (flags & IOFlags::OpenExclusive)
    openflags |= O_EXCL;

  if (flags & IOFlags::OpenTruncate)
    openflags |= O_TRUNC;

  IOFD newfd = EDM_IOFD_INVALID;
  dc_errno = 0;
  if ((newfd = dc_open (name, openflags, perms)) == -1)
    throw cms::Exception("DCacheFile::open()")
      << "dc_open(name='" << name
      << "', flags=" << openflags
      << ", permissions=" << perms
      << ") => error '" << dc_strerror(dc_errno)
      << "' (dc_errno=" << dc_errno << ")";

  m_fd = newfd;

  // Set the buffering readahead size for dCache.  The value chosen here was determined from
  // a study done by Ian Fisk 31-May-2008
  // This can make dramatic difference to the system and client
  // performance (factors of ten difference in the amount of data
  // read, and time spent reading). Note also that docs incorrectly
  // say the flag turns off only write buffering -- this affects
  // all buffering.
  // if (flags & IOFlags::OpenUnbuffered)
  //   dc_noBuffering (m_fd);

  dc_setBufferSize(m_fd, 64000);

  m_close = true;

  edm::LogInfo("DCacheFileInfo") << "Opened " << m_name;
}

void
DCacheFile::close (void)
{
  if (m_fd == EDM_IOFD_INVALID)
  {
    edm::LogError("DCacheFileError")
      << "DCacheFile::close(name='" << m_name
      << "') called but the file is not open";
    m_close = false;
    return;
  }

  dc_errno = 0;
  if (dc_close (m_fd) == -1)
    edm::LogWarning("DCacheFileWarning")
      << "dc_close(name='" << m_name
      << "') failed with error '" << dc_strerror (dc_errno)
      << "' (dc_errno=" << dc_errno << ")";

  m_close = false;
  m_fd = EDM_IOFD_INVALID;

  // Caused hang.  Will be added back after problem is fixed.
  // edm::LogInfo("DCacheFileInfo") << "Closed " << m_name;
}

void
DCacheFile::abort (void)
{
  if (m_fd != EDM_IOFD_INVALID)
    dc_close (m_fd);

  m_close = false;
  m_fd = EDM_IOFD_INVALID;
}

//////////////////////////////////////////////////////////////////////
static const int BUGLINE = __LINE__ + 1;
// Apparently dc_read can return short reads; I don't know if dc_write
// will also return short writes.  This is a bug in dCache.  POSIX,
// apparently contrary to the understanding of dCache authors, does
// not allow reads from files to return short, and in fact no network
// file system returns short reads.  For more details please refer to
// http://www.opengroup.org/onlinepubs/000095399/functions/read.html:
//    The value returned may be less than nbyte if the number of
//    bytes left in the file is less than nbyte, if the read()
//    request was interrupted by a signal, or if the file is a
//    pipe or FIFO or special file and has fewer than nbyte bytes
//    immediately available for reading.
// (In other words, barring signals (which should use SA_RESTART and
// in any case should not affect dCache) the only way a read from a
// file can return short is when there is nothing left to read.)
IOSize
DCacheFile::read (void *into, IOSize n)
{
  IOSize done = 0;
  while (done < n)
  {
    dc_errno = 0;
    ssize_t s = dc_read (m_fd, (char *) into + done, n - done);
    if (s == -1)
      throw cms::Exception("DCacheFile::read()")
        << "dc_read(name='" << m_name << "', n=" << (n-done)
        << ") failed with error '" << dc_strerror(dc_errno)
        << "' (dc_errno=" << dc_errno << ")";
    else if (s == 0)
      // end of file
      break;
    else if (s < ssize_t (n-done))
      edm::LogInfo("DCacheFileWarning")
        << "dc_read(name='" << m_name << "', n=" << (n-done)
        << ") returned a short read of " << s << " bytes; "
        << "please report a bug in dCache referencing the "
        << "comment on line " << BUGLINE << " of " << __FILE__;
    done += s;
  }

  return done;
}

IOSize
DCacheFile::write (const void *from, IOSize n)
{
  IOSize done = 0;
  while (done < n)
  {
    dc_errno = 0;
    ssize_t s = dc_write (m_fd, (const char *) from + done, n - done);
    if (s == -1)
      throw cms::Exception("DCacheFile::write()")
        << "dc_write(name='" << m_name << "', n=" << (n-done)
        << ") failed with error '" << dc_strerror(dc_errno)
        << "' (dc_errno=" << dc_errno << ")";
    else if (s < ssize_t (n-done))
      edm::LogWarning("DCacheFileWarning")
        << "dc_write(name='" << m_name << "', n=" << (n-done)
        << ") returned a short write of " << s << " bytes; "
        << "please report a bug in dCache referencing the "
        << "comment on line " << BUGLINE << " of " << __FILE__;
    done += s;
  }

  return done;
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
IOOffset
DCacheFile::position (IOOffset offset, Relative whence /* = SET */)
{
  if (m_fd == EDM_IOFD_INVALID)
    throw cms::Exception("DCacheFile::position()")
      << "DCacheFile::position() called on a closed file";
  if (whence != CURRENT && whence != SET && whence != END)
    throw cms::Exception("DCacheFile::position()")
      << "DCacheFile::position() called with incorrect 'whence' parameter";

  IOOffset	result;
  int		mywhence = (whence == SET ? SEEK_SET
		    	    : whence == CURRENT ? SEEK_CUR
			    : SEEK_END);

  dc_errno = 0;
  if ((result = dc_lseek64 (m_fd, offset, mywhence)) == -1)
    throw cms::Exception("DCacheFile::position()")
      << "dc_lseek64(name='" << m_name << "', offset=" << offset
      << ", whence=" << mywhence << ") failed with error '"
      << dc_strerror (dc_errno) << "' (dc_errno=" << dc_errno << ")";

  // FIXME: dCache returns incorrect value on SEEK_END.
  // Remove this hack when dcap has been fixed.
  if (whence == SEEK_END && (result = dc_lseek64 (m_fd, result, SEEK_SET)) == -1)
    throw cms::Exception("DCacheFile::position()")
      << "dc_lseek64(name='" << m_name << "', offset=" << offset
      << ", whence=" << SEEK_SET << ") failed with error '"
      << dc_strerror (dc_errno) << "' (dc_errno=" << dc_errno << ")";
  
  return result;
}

void
DCacheFile::resize (IOOffset /* size */)
{
  throw cms::Exception("DCacheFile::resize()")
    << "DCacheFile::resize(name='" << m_name << "') not implemented";
}
