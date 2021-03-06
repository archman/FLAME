
#include <sstream>

#include <H5Cpp.h>

#include "flame/h5loader.h"

// H5::Exception doesn't derive from std::exception
// so translate to some type which does.
// TODO: sub-class mixing H5::Exception and std::exception?
#define CATCH() catch(H5::Exception& he) { \
    std::ostringstream strm; \
    strm<<"H5 Error "<<he.getDetailMsg(); \
    throw std::runtime_error(strm.str()); \
    }

struct H5Loader::Pvt {
    H5::H5File file;
    H5::Group group;
};


H5Loader::H5Loader() :pvt(new Pvt) {}

H5Loader::H5Loader(const char *spec) :pvt(new Pvt)
{
    open(spec);
}

H5Loader::H5Loader(const std::string& spec) :pvt(new Pvt)
{
    open(spec);
}

H5Loader::~H5Loader()
{
    try{
        close();
    } catch(std::runtime_error& e) {
        std::cerr<<"H5Loader is ignoring exception in dtor : "<<e.what()<<"\n";
    }
    delete pvt;
}

void H5Loader::open(const char *spec)
{
    open(std::string(spec));
}

void H5Loader::open(const std::string& spec)
{
    close();
    /* The provided spec may contain both file path and group(s)
     * seperated by '/' which is ambigious as the file path
     * may contain '/' as well...
     * so do as h5ls does and strip off from the right hand side until
     * and try to open while '/' remain.
     */
    size_t sep = spec.npos;

    while(true) {
        sep = spec.find_last_of('/', sep-1);

        std::string fname(spec.substr(0, sep));

        try {
            pvt->file.openFile(fname, H5F_ACC_RDONLY);
        } catch(H5::FileIException& e) {
            if(sep==spec.npos) {
                // no more '/' so this is failure
                throw std::runtime_error("Unable to open file");
            }
            continue; // keep trying
        } CATCH()

        std::string group(spec.substr(sep+1));

        try {
            pvt->group = pvt->file.openGroup(group);
        } catch(H5::FileIException& e) {
            throw std::runtime_error("Unable to open group");
        } CATCH()

        return;
    }
}

void H5Loader::close()
{
    try{
        pvt->group.close();
        pvt->file.close();
    }CATCH()
}

H5Loader::matrix_t
H5Loader::load(const char * setname)
{
    H5::DataSet dset;
    try{
        dset = pvt->group.openDataSet(setname);
    }catch(H5::GroupIException& e) {
        throw std::runtime_error("H5 group does not exist");
    }CATCH()

    try{
        H5::DataSpace fspace(dset.getSpace());

        size_t N = fspace.getSimpleExtentNdims();
        if(N>2)
            throw std::runtime_error("Can't load > 2d as matrix");

        hsize_t fsize[2] = {1,1};
        fspace.getSimpleExtentDims(fsize);

        H5::DataSpace mspace(2, fsize);

        matrix_t ret(fsize[0], fsize[1]);

        fspace.selectAll();
        mspace.selectAll();

        matrix_t::array_type& storage(ret.data());
        dset.read(&storage[0], H5::PredType::NATIVE_DOUBLE, mspace, fspace);

        return ret;
    }CATCH()
}

H5Loader::matrix_t
H5Loader::load(const std::string& set)
{
    return load(set.c_str());
}

void H5Loader::dontPrint()
{
    try {
        H5::Exception::dontPrint();
    }CATCH()
}
