
#include "talk/base/logging.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include "mypktsocket.h"

#include "talk/base/asyncsocket.h"
#include "talk/base/asyncpacketsocket.h"

const int BUF_SIZE = 64 * 1024;

MyPktSocket::MyPktSocket(talk_base::AsyncSocket* socket, bool istcp) : talk_base::AsyncPacketSocket(socket) {
  size_ = BUF_SIZE;
  buf_ = new char[size_];
  istcp_ = istcp;
  throttle_ = false;
  pending_reads_ = 0;
  assert(socket_);
  // The socket should start out readable but not writable.
  socket_->SignalReadEvent.connect(this, &MyPktSocket::OnReadEvent);
  socket_->SignalCloseEvent.connect(this, &MyPktSocket::OnCloseEvent);
}

MyPktSocket::~MyPktSocket() {
  delete [] buf_;
}

void
MyPktSocket::UndoThrottle() {
	throttle_ = false;
	if (pending_reads_) {
		socket_->SignalReadEvent(socket_);
	}
}

void 
MyPktSocket::OnCloseEvent(talk_base::AsyncSocket *sock, int err) {
	SignalSocketClose(sock);
}

void MyPktSocket::OnReadEvent(talk_base::AsyncSocket* socket) {
  assert(socket == socket_);

  talk_base::SocketAddress remote_addr;

  if (throttle_) {
	  pending_reads_ ++;
	  return;
  } else {
	  if (pending_reads_ > 0) {
		pending_reads_ --;
	  }
  }
	
  int len;
  
  if (istcp_) {
	  len = socket_->Recv(buf_, size_);
  } else {
	  len = socket_->RecvFrom(buf_, size_, &remote_addr);
  }
  if (len < 0) {
    // TODO: Do something better like forwarding the error to the user.
    PLOG(LS_ERROR, socket_->GetError()) << "recvfrom";
	pending_reads_ = 0;
    return;
  }

  // TODO: Make sure that we got all of the packet.  If we did not, then we
  // should resize our buffer to be large enough.

  SignalReadPacket(buf_, (size_t)len, remote_addr, this);

  if (pending_reads_ > 0) {
	  socket_->SignalReadEvent(socket_);
  }
}
