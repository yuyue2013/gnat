#ifndef __MYPKTSOCKET_H__
#define __MYPKTSOCKET_H__

#include "talk/base/asyncpacketsocket.h"
#include "talk/base/socketfactory.h"


// Provides the ability to receive packets asynchronously.
class MyPktSocket : public talk_base::AsyncPacketSocket {
public:
	MyPktSocket(talk_base::AsyncSocket* socket, bool istcp);
  virtual ~MyPktSocket();
  talk_base::AsyncSocket *GetAsyncSocket() {return socket_;}
  void Throttle() {throttle_ = true;};
  void UndoThrottle();

   // Emitted each time a packet is read.
  sigslot::signal1<talk_base::AsyncSocket*> SignalSocketClose;
  
private:
  char* buf_;
  size_t size_;
  bool throttle_;
  int pending_reads_;
  bool istcp_;
  // Called when the underlying socket is ready to be read from.
  void OnReadEvent(talk_base::AsyncSocket* socket);
  void OnCloseEvent(talk_base::AsyncSocket* socket, int err);
};

#endif
