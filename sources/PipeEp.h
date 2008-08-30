#ifndef _PIPEEP_H_
#define _PIPEEP_H_

#define _CRT_SECURE_NO_DEPRECATE 1

enum {
	PIPE_EP_START_SRC,
	PIPE_EP_START_DST,
	PIPE_WRITE_STREAM,
	PIPE_WRITE_SOCKET,
	PIPE_EP_SHUTDOWN,
};

class MyPktSocket;

#define CONNECT_INTERVAL (20000)

class PipeEp : public talk_base::MessageHandler, public sigslot::has_slots<>
{
public:
	PipeEp(PipeClient* pump, 
		   talk_base::Thread *th,
		   talk_base::SocketServer *ss, 
		   bool src, std::string& lportstr, 
		   std::string& rportstr,
	       std::string& user,
		   bool istcp = true);

	virtual ~PipeEp(void);

	void StartSrc();

	void OnConnectEvent(talk_base::AsyncSocket *socket);

	void OnAcceptEvent(talk_base::AsyncSocket* socket);

	void InitiateTunnel();

	void ProcessStream();

	void OnStreamEvent(talk_base::StreamInterface* stream, int events, int error);

	void OnMessage(talk_base::Message *pmsg);

	bool DstConnect();

	void OnIncomingTunnel(cricket::TunnelSessionClient* client, buzz::Jid jid,
		std::string description, cricket::Session* session);

	void OnPacket(const char* buf, size_t size, const talk_base::SocketAddress& remote_addr,
		talk_base::AsyncPacketSocket* socket);

	void Show(void);

	void ConnectTimeout(void);

private:

	void WriteToStream(const char* buf, size_t size);
	void WriteToSocket();

	void OnSocketClose(talk_base::AsyncSocket *);
	void CleanupAll();
    void Shutdown();

	PipeClient *pump_;
	talk_base::Thread *thread_;
	talk_base::SocketServer *ss_;
	bool issrc_, istcp_;
	std::string lportstr_,rportstr_;
	int lport_,rport_;
	std::string user_;
	talk_base::AsyncSocket *socket_;      //server socket ep
	MyPktSocket *conn_socket_;
	talk_base::StreamInterface *stream_;  //tunnel stream interface
	cricket::TunnelSessionClient* client_;
	cricket::Session *session_;
	char read_buf_[1024*64];
	size_t read_buf_len_;
	char write_buf_[1024*64];
	size_t write_buf_len_;
	int pending_stream_read_;
	// stats
	size_t stream_bytes_,socket_bytes_;
	uint32 start_time_;
	// for udp
	talk_base::SocketAddress udp_rem_addr_;
};

#endif