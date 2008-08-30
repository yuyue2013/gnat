
#ifndef _PIPE_CLIENT_H_
#define _PIPE_CLIENT_H_

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

#ifndef _CRT_SECURE_NO_DEPRECATE 
#define _CRT_SECURE_NO_DEPRECATE 1
#endif

struct RosterItem {
  buzz::Jid jid;
  buzz::Status::Show show;
  std::string status;
};

class PipeEp;

class PipeClient : public sigslot::has_slots<>, public talk_base::MessageHandler {
public:
	  enum {
    MSG_STOP,
	MSG_TERMINATE_PIPE,
	MSG_CHECK_PIPE,
  };
	PipeClient::PipeClient(buzz::XmppClient *xmppclient, talk_base::SocketServer *ss);
  virtual ~PipeClient();
  void ParseLine(const std::string& line);
  virtual void OnMessage(talk_base::Message *m);
  cricket::TunnelSessionClient *GetTunnelClient(void) {return tunnel_client_.get();};

private:

  typedef std::map<std::string,RosterItem> RosterMap;

  void OnStateChange(buzz::XmppEngine::State state);
  void OnRequestSignaling();
  void OnJingleInfo(const std::string & relay_token,
                    const std::vector<std::string> &relay_addresses,
                    const std::vector<talk_base::SocketAddress> &stun_addresses);
  const char* DescribeStatus(buzz::Status::Show show, const std::string& desc);
  void PrintRoster();
  void OnStatusUpdate(const buzz::Status &status); 
  void OnSignon();
  void OnIncomingTunnel(cricket::TunnelSessionClient* client, buzz::Jid jid,
	  std::string description, cricket::Session* session);
  void ShowPipes(void);

  talk_base::NetworkManager network_manager_;
  talk_base::scoped_ptr<cricket::HttpPortAllocator> port_allocator_;
  talk_base::scoped_ptr<cricket::SessionManager> session_manager_;
  talk_base::scoped_ptr<cricket::TunnelSessionClient> tunnel_client_;
  buzz::XmppClient *xmpp_client_;
  talk_base::Thread *worker_thread_;
  RosterMap* roster_;
  talk_base::SocketServer *ss_;
  std::vector<PipeEp *> pipes_;
};

#endif