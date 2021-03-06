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
using grpc::ServerReader;
using grpc::Status;

using RpcPackage::LongMessage;
using RpcPackage::IntMessage;
using RpcPackage::BytesMessage;
using RpcPackage::ReadMessage;
using RpcPackage::StringMessage;
using RpcPackage::BooleanMessage;
using RpcPackage::StatStruct;
using RpcPackage::DirMessage;
using RpcPackage::DirEntry;
using RpcPackage::RpcService;

#define BUF_SIZE 4 * 1024

std::string* root_dir;

int get_file_links(string full_path){
	log("Call to get file hard links"); 
	struct stat attr;
	stat(full_path.c_str(), &attr);
	return (int)attr.st_nlink;
}

long get_file_modified_time(string full_path){
	log("Call to get file modified time"); 
	struct stat attr;
	stat(full_path.c_str(), &attr);
	long timestamp = (long)attr.st_mtime;
	return timestamp;
}

class Vice final: public RpcService::Service{
	
	Status unlinkfile(ServerContext* context, const StringMessage* path, BooleanMessage* reply) override {
		log("unlink file request received");
		string full_path = *root_dir + path->msg();
		int hard_links = get_file_links(full_path);
		int res = unlink(full_path.c_str());
		if(res == 0){
			if(hard_links == 1){
				reply->set_msg(true);
				log("delete file");
			}
			else{
				reply->set_msg(false);
				log("dont delete file");
			}
			return Status::OK;
		}
		else{
			log("unlink file failed");
			return Status::CANCELLED;
		}
	}
	
	Status removedir(ServerContext* context, const StringMessage* path, BooleanMessage* reply) override {
		log("remove dir request received");
		string full_path = *root_dir + path->msg();
		int res = rmdir(full_path.c_str());		
		if(res == 0){
			return Status::OK;
		}
		else{
			return Status::CANCELLED;
		}
	}
	
	Status makedir(ServerContext* context, const StringMessage* path, BooleanMessage* reply) override {
		log("mkdir request received");
		string full_path = *root_dir + path->msg();
		int res = mkdir(full_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);		
		if(res == 0){
			return Status::OK;
		}
		else{
			return Status::CANCELLED;
		}
	}

	Status writefile(ServerContext* context, ServerReader<BytesMessage>* reader, LongMessage* reply) override {
		log("writefile request received");
		string dest_file_path;
		string temp_file_path = "/tmp/temp_file";
		bool first = true;
		FILE* writefile = fopen(temp_file_path.c_str(), "wb");
		int writefd = fileno(writefile);
		if(writefd == -1){
			log("Error in opening temp file");
		}
		BytesMessage recv_msg;
		while (reader->Read(&recv_msg)){
			log(recv_msg.msg());
			log("size: %i", recv_msg.size());
			if(first){
				dest_file_path = *root_dir + recv_msg.msg();
				first = false;
			}
			else{
				log("Not first message");
				int res = fwrite(recv_msg.msg().c_str(), 1, recv_msg.size(), writefile);
				log("wrote %i", res);
				//outfile.write(recv_msg.msg().c_str(), recv_msg.size());
			}
		}
		fsync(writefd);
		log("Synced file..closing");
		fclose(writefile);
		int result = rename(temp_file_path.c_str(), dest_file_path.c_str());
		if(result == 0){
			reply->set_msg(get_file_modified_time(dest_file_path));
			return Status::OK;
		}
		else{
			return Status::CANCELLED;
		}
	}

	Status filetime(ServerContext* context, const StringMessage* recv_msg, LongMessage* reply) override {
		log("filetime request received");
		string full_path = *root_dir + recv_msg->msg();
		log(full_path);
		long timestamp = get_file_modified_time(full_path); 
		reply->set_msg(timestamp);
		return Status::OK;
	}

	Status readfile(ServerContext* context, const StringMessage* recv_msg, ServerWriter<BytesMessage>* writer) override {
		log("read request received");
		string full_path = *root_dir + recv_msg->msg();
		log(full_path);
		FILE *pFile = fopen(full_path.c_str() , "rb");
		if (pFile == NULL) {
			log("Error in opening file"); 
			return Status::CANCELLED;
		}
		long timestamp = get_file_modified_time(full_path);
		char buffer[BUF_SIZE];
		for(;;){
			size_t n = fread(buffer, 1, BUF_SIZE, pFile);
			BytesMessage bytes;
			log("Read %i", n);
			bytes.set_msg(buffer, n);
			bytes.set_size(n);
			bytes.set_timestamp(timestamp);
			writer->Write(bytes);	
			if (n < BUF_SIZE) { 
				log("breaking");
				break; 
			}
		}
		log("closing read file"); 
		fclose(pFile);
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
		if(res == -1){
			return Status::CANCELLED;
		}
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
	//need to get rid of any temp file on restart
	open_err_log();
	root_dir = new string(argv[1]);
	run_server();
	return 0;
}
