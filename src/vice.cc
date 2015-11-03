/**
 * The vice server
 */

#include <iostream>
#include <string>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <string>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <grpc++/grpc++.h>

#include "../proto/AFSInterface.grpc.pb.h"

#include "util.h"

using std::cout;
using std::endl;
using std::string;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::Status;

using RpcPackage::IntMessage;
using RpcPackage::BytesMessage;
using RpcPackage::ReadMessage;
using RpcPackage::StringMessage;
using RpcPackage::StatStruct;
using RpcPackage::DirMessage;
using RpcPackage::DirEntry;
using RpcPackage::RpcService;

std::string* root_dir;
std::map<int, std::map<string, long> >* files_checked_out;

class Vice final: public RpcService::Service{

	Status readfile(ServerContext* context, const ReadMessage* recv_msg, ServerWriter<BytesMessage>* writer) override {
		log("read request received");
		string full_path = *root_dir + recv_msg->path();
		log(full_path);
		int clientid = recv_msg->clientid();
		std::fstream inf(full_path, std::ios::in);
		char data;
		int bytes = 0;
		while( !inf.eof() ) {
			log("%c", data);
			bytes++;
			BytesMessage f;
			f.set_msg(&data);
			writer->Write(f);
    		}
		log("Bytes read: %i", bytes);
		inf.close();
		// log as checked out
		//files_checkout
		log("--------------------------------------------------");
		return Status::OK;
	}

	Status stat_get_attr(ServerContext* context, const StringMessage* recv_msg, StatStruct* reply_struct) override {
		log("get_attr request received");
		struct stat stbuf;
		memset(&stbuf, 0, sizeof(struct stat));
		string full_path = *root_dir + recv_msg->msg();
		log(full_path);
		int res = lstat(full_path.c_str(), &stbuf);
		reply_struct->set_file_number(stbuf.st_ino);
		reply_struct->set_time_access(stbuf.st_atime);
		reply_struct->set_time_mod(stbuf.st_mtime);
		reply_struct->set_time_chng(stbuf.st_ctime);
		reply_struct->set_file_mode(stbuf.st_mode);
		reply_struct->set_hard_links(stbuf.st_nlink);
		reply_struct->set_file_size(stbuf.st_size);
		log("--------------------------------------------------");
		return Status::OK;
	}

	Status readdirectory(ServerContext* context, const StringMessage* recv_msg, DirMessage* reply_msg) override {
		log("readdir request received");
		DIR *dp;
		struct dirent *de;
		string full_path = *root_dir + recv_msg->msg();
		log(full_path);
		dp = opendir(full_path.c_str());
		if (dp == NULL){
			log("Dir is Null");
			return Status::OK;
		}
		while ((de = readdir(dp)) != NULL) {
			DirEntry* ent = reply_msg->add_dir();
			ent->set_file_number(de->d_ino);
			ent->set_file_mode(de->d_type<<12);
			ent->set_name(de->d_name);
			log(de->d_name);
		}
		reply_msg->set_success(true);
		closedir(dp);
		log("--------------------------------------------------");
		return Status::OK;
	}

};

void run_server() {
	std::string server_address("0.0.0.0:50051");
	Vice service;

	ServerBuilder builder;
	// Listen on the given address without any authentication mechanism.
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	// Register "service" as the instance through which we'll communicate with
	// clients. In this case it corresponds to an *synchronous* service.
	builder.RegisterService(&service);
	// Finally assemble the server.
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout << "Vice Server listening on " << server_address << std::endl;

	// Wait for the server to shutdown. Note that some other thread must be
	// responsible for shutting down the server for this call to ever return.
	server->Wait();
}

int main(int argc, char** argv) {
	open_err_log();
	root_dir = new string(argv[1]);
	files_checked_out = new std::map<int, std::map<string, long> >();
	run_server();
	return 0;
}
