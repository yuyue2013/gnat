
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

#include "PipeClient.h"
#include "PipeEp.h"

const char* CONSOLE_COMMANDS =
"Available commands:\n"
"\n"
"  roster       - Prints the online friends from your roster.\n"
"  pipe jid local-port remote-port [udp|tcp] -  Initiate a pipe.\n"
"  show  - shows state of pipes\n"
"  quit     -    Quits the application.\n"
"";


PipeClient::PipeClient(buzz::XmppClient *xmppclient, talk_base::SocketServer *ss) :
    xmpp_client_(xmppclient),
	ss_(ss)
{
	roster_ = new RosterMap;
	worker_thread_ = NULL;
	xmppclient->SignalStateChange.connect(this, &PipeClient::OnStateChange);
}


PipeClient::~PipeClient() {
	delete roster_;
}

void 
PipeClient::OnStateChange(buzz::XmppEngine::State state) {
    switch (state) {
    case buzz::XmppEngine::STATE_START:
      std::cout << "Connecting..." << std::endl;
      break;
    case buzz::XmppEngine::STATE_OPENING:
      std::cout << "Logging in. " << std::endl;
      break;
    case buzz::XmppEngine::STATE_OPEN:
      std::cout << "Logged in as " << xmpp_client_->jid().Str() << std::endl;
      OnSignon();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      std::cout << "Logged out." << std::endl;
      break;
    }
}

void 
PipeClient::ParseLine(const std::string& line) {
  std::vector<std::string> words;
  int start = -1;
  int state = 0;
  for (int index = 0; index <= static_cast<int>(line.size()); ++index) {
    if (state == 0) {
      if (!isspace(line[index])) {
        start = index;
        state = 1;
      }
    } else {
      assert(state == 1);
      assert(start >= 0);
      if (isspace(line[index])) {
        std::string word(line, start, index - start);
		words.push_back(word);
        start = -1;
        state = 0;
      }
    }
  }

  // Global commands
  if ((words.size() == 1) && (words[0] == "quit")) {
      exit(0);
  }
  if ((words.size() == 1) && (words[0] == "show")) {
      ShowPipes();
	  return;
  }
  if ((words.size() == 1) && (words[0] == "roster")) {
      PrintRoster();
  } else if (((words.size() == 4) || (words.size() == 5)) && (words[0] == "pipe")) {
    bool istcp = true;
	if ((words.size() == 5) && words[4] == "udp") {
		istcp = false;
	}
	talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
	PipeEp *pipe = new PipeEp(this, thread, thread->socketserver(),true,words[2],words[3],words[1], istcp);
	pipes_.push_back(pipe);
	thread->Post(pipe,PIPE_EP_START_SRC);
  } else {
      printf("%s\n",CONSOLE_COMMANDS);
  }
}


void 
PipeClient::OnRequestSignaling() {
	session_manager_.get()->OnSignalingReady();
}

void 
PipeClient::OnJingleInfo(const std::string & relay_token,
                    const std::vector<std::string> &relay_addresses,
                    const std::vector<talk_base::SocketAddress> &stun_addresses) {
	std::vector<std::string>::const_iterator i;
	std::cout << "relay token: " << relay_token << std::endl;
	std::cout << "relay addresses " << relay_addresses.size() << std::endl;
	for (i = relay_addresses.begin(); i != relay_addresses.end(); i++) {
		std::cout << *i << std::endl;
	}
	std::cout << "stun addresses " << stun_addresses.size() << std::endl;
	std::vector<talk_base::SocketAddress>::const_iterator j;
	for (j = stun_addresses.begin(); j != stun_addresses.end(); j++) {
		std::cout << (*j).ToString() << std::endl;
	}
    port_allocator_->SetStunHosts(stun_addresses);
    port_allocator_->SetRelayHosts(relay_addresses);
    port_allocator_->SetRelayToken(relay_token);
}

const char* 
PipeClient::DescribeStatus(buzz::Status::Show show, const std::string& desc) {
	switch (show) {
		case buzz::Status::SHOW_XA:      return desc.c_str();
		case buzz::Status::SHOW_ONLINE:  return "online";
		case buzz::Status::SHOW_AWAY:    return "away";
		case buzz::Status::SHOW_DND:     return "do not disturb";
		case buzz::Status::SHOW_CHAT:    return "ready to chat";
		default:                         return "offline";
	}
}

void 
PipeClient::PrintRoster() {	
	printf("Roster contains %d callable\n", roster_->size());
	RosterMap::iterator iter = roster_->begin();
	while (iter != roster_->end()) {
		printf("%s/%s - %s\n",
                    iter->second.jid.BareJid().Str().c_str(),
					iter->second.jid.resource().c_str(),
                    DescribeStatus(iter->second.show, iter->second.status));
		iter++;
	}
}

  
void 
PipeClient::OnStatusUpdate(const buzz::Status &status) {
	std::cout << "update from " << status.jid().Str() << std::endl;

	RosterItem item;
	item.jid = status.jid();
	item.show = status.show();
	item.status = status.status();

	std::string key = item.jid.Str();

	if (status.available() && status.fileshare_capability()) {
		printf("Adding to roster: %s\n", key.c_str());
		(*roster_)[key] = item;
	} else {
		printf("Removing from roster: %s\n", key.c_str());
		RosterMap::iterator iter = roster_->find(key);
		if (iter != roster_->end())
			roster_->erase(iter);
	}
    if (status.available() && status.fileshare_capability()) {

      // A contact's status has changed. If the person we're looking for is online and able to receive
      // files, send it.
    }
  }
  
void 
PipeClient::OnMessage(talk_base::Message *m) {

	switch (m->message_id) {
	  case MSG_STOP:
		  {
			talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
			//    delete session_;
			thread->Stop();
			break;
		  }
	  case MSG_TERMINATE_PIPE:
		  {
		    printf("in pipe client terminating pipe\n");
			PipeEp *pipe = (PipeEp*)(m->pdata);
			delete pipe;
			for (std::vector<PipeEp*>::iterator it = pipes_.begin();
				  it != pipes_.end();
				++it) {
					  if (*it == pipe) {
						pipes_.erase(it);
						break;
					}
			}
		  }
		  break;
	  case MSG_CHECK_PIPE:
		  {
		    printf("checking pipe liveness\n");
			PipeEp *pipe = (PipeEp*)(m->pdata);
			for (std::vector<PipeEp*>::iterator it = pipes_.begin();
				  it != pipes_.end();
				++it) {
					  if (*it == pipe) {
						pipe->ConnectTimeout();
						return;
			          }
			}
		  }
		  break;
	}
}

void
PipeClient::ShowPipes(void) {
	for (std::vector<PipeEp*>::iterator it = pipes_.begin();
		  it != pipes_.end();
		++it) 
	{
	  (*it)->Show();
	}
}


void 
PipeClient::OnSignon() {
    std::string client_unique = xmpp_client_->jid().Str();
    cricket::InitRandom(client_unique.c_str(), client_unique.size());

	worker_thread_ = new talk_base::Thread();

    buzz::PresencePushTask *presence_push_ = new buzz::PresencePushTask(xmpp_client_);
    presence_push_->SignalStatusUpdate.connect(this, &PipeClient::OnStatusUpdate);
    presence_push_->Start();
    
    buzz::Status my_status;
    my_status.set_jid(xmpp_client_->jid());
    my_status.set_available(true);
    my_status.set_show(buzz::Status::SHOW_ONLINE);
    my_status.set_priority(0);
    my_status.set_know_capabilities(true);
    my_status.set_fileshare_capability(true);
    my_status.set_is_google_client(true);
    my_status.set_version("1.0.0.66");

    buzz::PresenceOutTask* presence_out_ =
      new buzz::PresenceOutTask(xmpp_client_);
    presence_out_->Send(my_status);
    presence_out_->Start();

	port_allocator_.reset(new cricket::HttpPortAllocator(&network_manager_, "pcp"));

    session_manager_.reset(new cricket::SessionManager(port_allocator_.get(), 
		worker_thread_));

	session_manager_.get()->SignalRequestSignaling.connect(
      this, &PipeClient::OnRequestSignaling);
    session_manager_.get()->OnSignalingReady();

    cricket::SessionManagerTask * session_manager_task = new cricket::SessionManagerTask(xmpp_client_, session_manager_.get());
    session_manager_task->EnableOutgoingMessages();
    session_manager_task->Start();
  
    buzz::JingleInfoTask *jingle_info_task = new buzz::JingleInfoTask(xmpp_client_);
    jingle_info_task->RefreshJingleInfoNow();
    jingle_info_task->SignalJingleInfo.connect(this, &PipeClient::OnJingleInfo);
    jingle_info_task->Start();    

	tunnel_client_.reset(new cricket::TunnelSessionClient(xmpp_client_->jid(), session_manager_.get()));
	tunnel_client_.get()->SignalIncomingTunnel.connect(this, &PipeClient::OnIncomingTunnel);

	worker_thread_->Start();
	//session_client.SignalSendStanza.connect(&pump, &CustomXmppPump::OnSendStanza);
    //SessionClientTask *receiver =
    //  new SessionClientTask(pump.client(), &session_client);
    //receiver->Start();

  }

void 
PipeClient::OnIncomingTunnel(cricket::TunnelSessionClient* client, buzz::Jid jid,
							 std::string description, cricket::Session* session) {

    std::cout << "IncomingTunnel from " << jid.Str()
      << ": " << description << std::endl;

    if (strncmp(description.c_str(), "pipe", 4) == 0) {
		// hand over to the pipe ep
		bool istcp;
		if (strncmp(description.c_str(), "pipetcp:", 8) == 0) {
			istcp = true;
		} else if (strncmp(description.c_str(), "pipeudp:", 8) == 0) {
			istcp = false;
		} else {
			printf("unknown protocol, rejecting\n");
			client->DeclineTunnel(session);
			return;
		}
		std::string lport("0");
		std::string user("none");
	    talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
		PipeEp *pipe = new PipeEp(this,thread,thread->socketserver(),false,lport,description.substr(8),user, istcp);
		pipes_.push_back(pipe);
		pipe->OnIncomingTunnel(client,jid,description,session);
		thread->Post(pipe,PIPE_EP_START_DST);
	} else {
	  printf("not a pipe request, rejecting\n");
      client->DeclineTunnel(session);
    }
}