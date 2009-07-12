//-*- mode: c++; tab-width: 4; indent-tabs-mode: t; c-file-style: "stroustrup";
#include <lsfs.hh>
#include <boost/program_options.hpp>
#include <iostream>

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    po::options_description desc("Usage: mkfs.lsfs [OPTIONS]... [DEVICE]\n\nMake a lsfs file system");
    uint64_t maxfiles=1024;
    uint64_t maxchunks=128;
    std::string dev;
    desc.add_options()
	("help,h","This help message.")
	("maxfiles,f",po::value<uint64_t>(&maxfiles),"Maximum number of files the filesystem will support")
	("maxchunks,c",po::value<uint64_t>(&maxchunks),"Maxinum number of chunks a file can be split into")
	("device,d",po::value<std::string>(&dev),"The device file to format");
    po::positional_options_description pd; 
    pd.add("device", 1);
    
    try {
	po::variables_map vm;
	po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).positional(pd).run(); 
	po::store(parsed, vm); 
	po::notify(vm);
	po::notify(vm);
	if (vm.count("help")) {
	    std::cout << desc << std::endl;
	    return 0;
	} 
	if(dev == "") throw po::error("you must specify a device file");
	std::cout << "Creating filesystem" << std::endl;
	lsfs::FS::create(dev, maxfiles, maxchunks);
	std::cout << "   Done!" << std::endl;
    } catch(po::error & e) {
	std::cerr << e.what() << std::endl;
	std::cerr << desc << std::endl;
	return 1;
    } catch(lsfs::InternalError & e) {
	std::cerr << e.what() << std::endl;
	return 1;
    } catch(lsfs::ErrnoException & e) {
	std::cerr << e.what() << std::endl;
	return 1;
    }
    return 0;
}
