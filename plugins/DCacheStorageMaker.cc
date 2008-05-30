#include "Utilities/StorageFactory/interface/StorageMaker.h"
#include "Utilities/StorageFactory/interface/StorageMakerFactory.h"
#include "Utilities/DCacheAdaptor/interface/DCacheFile.h"
#include <unistd.h>
#include <dcap.h>

class DCacheStorageMaker : public StorageMaker
{
  /** Return appropriate path for use with dcap library.  If the path is in the
      URL form ([gsi]dcap://host:port/path), return the full URL as it is.  If
      the path is /pnfs form (dcap:/pnfs/path), return only the path part, unless
      the protocol is 'gsidcap', in which case return the whole thing.  */
  static std::string normalise (const std::string &proto, const std::string &path)
  {
    size_t p = path.find("/pnfs");
    if (p < 3)
      return (proto == "gsidcap") ? proto + ':' + path.substr(p) : path.substr(p);

    // remove multiple "/"
    p = path.find_first_not_of('/');

    // it's url, return the full thing
    return proto + "://" + path.substr(p);
  }

public:
  /** Open a storage object for the given URL (protocol + path), using the
      @a mode bits.  No temporary files are downloaded.  */
  virtual Storage *open (const std::string &proto,
			 const std::string &path,
			 int mode,
			 const std::string &tmpdir,
                         int dcacheBufferSize)
  {
    int perms = 0666;
    return new DCacheFile (normalise (proto, path), mode, perms, dcacheBufferSize);
  }

  virtual bool check (const std::string &proto,
		      const std::string &path,
		      IOOffset *size = 0)
  {
    std::string testpath (normalise (proto, path));
    if (dc_access (testpath.c_str (), R_OK) != 0)
      return false;

    if (size)
    {
      struct stat64 buf;
      if (dc_stat64 (testpath.c_str (), &buf) != 0)
        return false;
	
      *size = buf.st_size;
    }

    return true;
  }
};

DEFINE_EDM_PLUGIN (StorageMakerFactory, DCacheStorageMaker, "dcache");
DEFINE_EDM_PLUGIN (StorageMakerFactory, DCacheStorageMaker, "dcap");
DEFINE_EDM_PLUGIN (StorageMakerFactory, DCacheStorageMaker, "gsidcap");
