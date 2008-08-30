

#include <iomanip>
#include <time.h>

#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/ssladapter.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/examples/login/xmppthread.h"
#include "talk/examples/login/xmppauth.h"
#include "talk/p2p/client/httpportallocator.h"
#include "talk/p2p/client/sessionmanagertask.h"
#include "talk/examples/login/presencepushtask.h"
#include "talk/examples/login/presenceouttask.h"
#include "talk/examples/login/jingleinfotask.h"
#include "talk/session/tunnel/tunnelsessionclient.h"
#include "talk/base/host.h"
#include "talk/base/time.h"

#include "PipeClient.h"
#include "PipeEp.h"
#include "MyPktSocket.h"

using namespace talk_base;

PipeEp::PipeEp(PipeClient* pump, 
			   Thread *th, 
			   SocketServer *ss, bool src, std::string& lportstr, std::string& rportstr,
		std::string& user, bool istcp)
		: pump_(pump),
		  thread_(th),
		  ss_(ss),
		  issrc_(src),
		  lportstr_(lportstr),
		  rportstr_(rportstr),
		  user_(user),
		  istcp_(istcp) {
	socket_ = 0;
	conn_socket_ = 0;
	stream_ = 0;
	read_buf_len_ = 0;
	write_buf_len_ = 0;
	pending_stream_read_ = 0;
	stream_bytes_ = socket_bytes_ = 0;
	start_time_ = talk_base::Time();
	printf("istcp = %s\n", istcp?"true":"false");
	sscanf(lportstr_.c_str(),"%d",&lport_);
	sscanf(rportstr_.c_str(),"%d",&rport_);
	printf("loc port = %d, rport = %d\n", lport_, rport_);
}

PipeEp::~PipeEp(void) {
}

void 
PipeEp::StartSrc() {
	if (istcp_) {
		socket_ = thread_->socketserver()->CreateAsyncSocket(SOCK_STREAM);
		SocketAddress laddr(LocalHost().networks()[1]->ip(), lport_);
		if (socket_->Bind(laddr) < 0) {
			printf("bind to %s failed\n", laddr.ToString().c_str());
			CleanupAll();
			return;
		}
		socket_->Listen(5);
		socket_->SignalReadEvent.connect(this, &PipeEp::OnAcceptEvent);
		printf("src server started, listening at %s\n", laddr.ToString().c_str());
	} else {
		socket_ = thread_->socketserver()->CreateAsyncSocket(SOCK_DGRAM);
		SocketAddress laddr(LocalHost().networks()[1]->ip(), lport_);
		if (socket_->Bind(laddr) < 0) {
			printf("bind to %s failed\n", laddr.ToString().c_str());
			CleanupAll();
			return;
		}
		conn_socket_ = new MyPktSocket(socket_, istcp_);
		conn_socket_->SignalReadPacket.connect(this, &PipeEp::OnPacket);
		conn_socket_->SignalSocketClose.connect(this, &PipeEp::OnSocketClose);
		printf("initiating tunnel to user %s\n", user_.c_str());
		InitiateTunnel();
	}
}

void 
PipeEp::OnAcceptEvent(AsyncSocket* socket) {
	SocketAddress raddr;
	AsyncSocket *newsocket = static_cast<AsyncSocket*>(socket_->Accept(&raddr));
	if (newsocket) {
		printf("accepted connection from %s\n", raddr.ToString().c_str());
		conn_socket_ = new MyPktSocket(newsocket, istcp_);
		conn_socket_->SignalReadPacket.connect(this, &PipeEp::OnPacket);
		conn_socket_->SignalSocketClose.connect(this, &PipeEp::OnSocketClose);

		printf("initiating tunnel to user %s\n", user_.c_str());
		InitiateTunnel();
		thread_->PostDelayed(CONNECT_INTERVAL, pump_, 
			PipeClient::MSG_CHECK_PIPE, (MessageData*)this);
		newsocket->SignalReadEvent(newsocket);
	}
}

void
PipeEp::OnMessage(Message *msg) {
	switch (msg->message_id) {
		case PIPE_WRITE_SOCKET:
			WriteToSocket();
			break;
		case PIPE_WRITE_STREAM:
			WriteToStream(0,0);
			break;
		case PIPE_EP_START_SRC:
			StartSrc();
			break;
		case PIPE_EP_START_DST:
			printf("started dst\n");
			break;
		case PIPE_EP_SHUTDOWN:
			printf("shutting down\n");
			Shutdown();
			break;
	}
}

void 
PipeEp::InitiateTunnel() {
	printf("in initiate tunnel to %s\n", user_.c_str());
	stream_ = NULL;
	buzz::Jid jid_arg(user_);
    std::string message("pipe");
	if (istcp_) {
		message.append("tcp:");
	} else {
		message.append("udp:");
	}
    message.append(rportstr_);
    stream_ = pump_->GetTunnelClient()->CreateTunnel(jid_arg, message);
    ProcessStream();

}

void 
PipeEp::ProcessStream() { 
	stream_->SignalEvent.connect(this, &PipeEp::OnStreamEvent);
	if (stream_->GetState() == SS_OPEN) {
		//OnStreamEvent(stream_, SE_OPEN | SE_READ | SE_WRITE, 0);
	}
}

void
PipeEp::WriteToSocket() {
	int count;
	size_t write_pos = 0;
	if (!conn_socket_ || !stream_) {
		printf("socket not ready yet\n");
		return;
	}
	//printf("in write to socket read_buf_len = %d\n",read_buf_len_);
	while (write_pos < read_buf_len_) {
		if (istcp_) {
			count = conn_socket_->Send(read_buf_ + write_pos, read_buf_len_ - write_pos);
		} else {
			count = conn_socket_->SendTo(read_buf_ + write_pos, read_buf_len_ - write_pos,
				udp_rem_addr_);
		}
		if (count > 0) {
			write_pos += count;
			socket_bytes_ += count;
			//printf("writepos is %d\n",write_pos);
			continue;
		}
		if (count < 0) {
			read_buf_len_ -= write_pos;
			memmove(read_buf_, read_buf_ + write_pos, read_buf_len_);
			//printf("write blocked write buf len=%d\n",read_buf_len_);
			if (read_buf_len_ > 0) {
				ThreadManager::CurrentThread()->PostDelayed(100, this, PIPE_WRITE_SOCKET);
			}
			return;
		}
	}
	read_buf_len_ = 0;
	if (pending_stream_read_ > 0) {
		OnStreamEvent(stream_, SE_READ, 0);
	}
}

void 
PipeEp::OnStreamEvent(StreamInterface* stream, int events, int error) {
	size_t count;
	int err,temp;
	if (events & SE_CLOSE) {
		if (error == 0) {
			std::cout << "Tunnel closed normally" << std::endl;
		} else {
			std::cout << "Tunnel closed with error: " << error << std::endl;
		}
		CleanupAll();
		return;
	}
	if (events & SE_OPEN) {
		std::cout << "Tunnel connected" << std::endl;
		if (!issrc_) {
			DstConnect();
		}
	}
	if ((events & SE_WRITE)) {
		//printf("found write event, ignore\n");
	}
	if ((events & SE_READ)) {
		//printf("found read event\n");
		if (read_buf_len_ == sizeof(read_buf_)) {
			//printf("read buf full\n");
			pending_stream_read_++;
			WriteToSocket();
			return;
		}
		if (pending_stream_read_ > 0) {
			pending_stream_read_--;
		}
		StreamResult result = stream->Read(read_buf_+read_buf_len_,
                                sizeof(read_buf_)-read_buf_len_,
                                &count, &err);
		switch (result) {
			case SR_SUCCESS:
				// write to the socket
				//printf("read %d from stream\n", count);
				read_buf_len_ += count;
				WriteToSocket();
				
				break;
			case SR_EOS:
				printf("end of stream seen\n");
				CleanupAll();
				break;
			case SR_BLOCK:
				//printf("str blocked\n");
				break;

			default:
				printf("stream read failed\n");
				;
		}
	}
}


void 
PipeEp::OnConnectEvent(AsyncSocket *socket) {
	printf("socket connected!\n");
		printf("connect succeeded\n");
	conn_socket_ = new MyPktSocket(socket, istcp_);
	conn_socket_->SignalReadPacket.connect(this, &PipeEp::OnPacket);
	conn_socket_->SignalSocketClose.connect(this, &PipeEp::OnSocketClose);

	socket->SignalReadEvent(socket);
}


void
PipeEp::Shutdown() {
	// cleanup the socket
	printf("cleaning up everything\n");
	if (conn_socket_) {
		printf("cleaning up socket\n");
		conn_socket_->Close();
		delete conn_socket_;
		conn_socket_ = 0;
	}

	if (issrc_ && socket_) {
		delete socket_;
	}
	// cleanup the stream
	if (stream_) {
		printf("cleaning up stream\n");
		stream_->Close();
		stream_ = 0;
	}

	// cleanup the PipeEp in PipeClient
	thread_->Post(pump_, PipeClient::MSG_TERMINATE_PIPE, 
		(talk_base::MessageData*)this);
	printf("cleanup done\n");
}

void
PipeEp::CleanupAll() {
	thread_->Post(this, PIPE_EP_SHUTDOWN);
}

void
PipeEp::Show(void) {
	printf("----%s(%d,%d, %s)\n", user_.c_str(), lport_, rport_, istcp_?"tcp":"udp");
	uint32 current_ = talk_base::Time();
	printf("%d,%d\n",stream_bytes_,socket_bytes_);
	printf("rate = %d bps, curr=%d, start=%d\n",(stream_bytes_*8)*1000/(current_-start_time_+1),
		current_, start_time_);
}

void
PipeEp::ConnectTimeout(void) {
	if (!conn_socket_) {
		printf("connect timed out!, cleaning up\n");
		CleanupAll();
	} else {
		printf("is connected!\n");
	}
}

void
PipeEp::OnSocketClose(AsyncSocket *sock) {

	printf("SOCKET CLOSED!\n");
	CleanupAll();
}

bool
PipeEp::DstConnect() {
	// create an async client sock,
	// connect to remport
	if (istcp_) {
		socket_ = thread_->socketserver()->CreateAsyncSocket(SOCK_STREAM);
	} else {
		socket_ = thread_->socketserver()->CreateAsyncSocket(SOCK_DGRAM);
	}
	SocketAddress laddr(0,0);
	SocketAddress saddr(LocalHost().networks()[1]->ip(), rport_);
	if (socket_->Bind(laddr) < 0) {
		printf("bind to %s failed\n", laddr.ToString().c_str());
		//exit(1);
		CleanupAll();
		return false;
	}
	if (istcp_) {
		socket_->SignalConnectEvent.connect(this, &PipeEp::OnConnectEvent);
		if (socket_->Connect(saddr)) {
			printf("connect in progress to %s\n", saddr.ToString().c_str());
		}
		thread_->PostDelayed(CONNECT_INTERVAL, pump_, 
				PipeClient::MSG_CHECK_PIPE, (MessageData*)this);
	} else {
		conn_socket_ = new MyPktSocket(socket_, istcp_);
		conn_socket_->SignalReadPacket.connect(this, &PipeEp::OnPacket);
		conn_socket_->SignalSocketClose.connect(this, &PipeEp::OnSocketClose);
		udp_rem_addr_ = saddr;
	}
	return true;
}

void 
PipeEp::OnIncomingTunnel(cricket::TunnelSessionClient* client, buzz::Jid jid,
						 std::string description, cricket::Session* session) {

		std::cout << "IncomingTunnel from " << jid.Str()
		<< ": " << description << std::endl;

		session_ = session;
		client_ = client;

		stream_ = client_->AcceptTunnel(session_);
		ProcessStream();

}


void 
PipeEp::WriteToStream(const char* buf, size_t size) {
	size_t count;
	int error;
	StreamResult result;
	//printf("in write to stream, len = %d\n", write_buf_len_);

	if (!stream_ || !conn_socket_) {
		return;
	}
	if (size) {
		memcpy(write_buf_+write_buf_len_, buf, size);
		write_buf_len_ += size;
	}
	size_t write_pos = 0;
	while (write_pos < write_buf_len_) {
		result = stream_->Write(write_buf_ + write_pos, write_buf_len_ - write_pos,
                                &count, &error);
		if (result == SR_SUCCESS) {
			write_pos += count;
			stream_bytes_ += count;
			//printf("writepos is %d\n",write_pos);
			continue;
		}
		if (result == SR_BLOCK) {
			write_buf_len_ -= write_pos;
			memmove(write_buf_, write_buf_ + write_pos, write_buf_len_);
			//printf("write blocked write buf len=%d\n",write_buf_len_);
			if (write_buf_len_ > 0) {
				//printf("throttling\n");
				conn_socket_->Throttle();
				ThreadManager::CurrentThread()->PostDelayed(100, this, PIPE_WRITE_STREAM);
			}
			return;
		}
		if (result == SR_EOS) {
					std::cout << "Tunnel closed unexpectedly on write" << std::endl;
		} else {
					std::cout << "Tunnel write error: " << error << std::endl;
		}
		//exit(1);
		//Cleanup(stream);
		CleanupAll();
		return;
	}
	//printf("undoing throttle\n");
	write_buf_len_ = 0;
	conn_socket_->UndoThrottle();

}

void 
PipeEp::OnPacket(const char* buf, size_t size, const SocketAddress& remote_addr,
		AsyncPacketSocket* socket) {

	//printf("read %d from socket\n", size);
	//printf("rcvd from %s\n", remote_addr.ToString().c_str());

	if (!istcp_) {
		// for udp check the remote address
		if (!issrc_ && (remote_addr != udp_rem_addr_)) {
			printf("rem addr mismatch\n");
			return;
		} else {
			
		}
	}

	WriteToStream(buf, size);

 }