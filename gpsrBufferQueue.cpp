#ifndef _GPSRBUFFERQUEUE_CPP
#define _GPSRBUFFERQUEUE_CPP

#include "gpsrBufferQueue.hpp"
#include  "gpsr.h"


unsigned 
GPSRJumpoutTimer::ttls_[4] = { 4, 8,  IP_DEF_TTL/2,  IP_DEF_TTL }; 

GPSRJumpoutTimer::GPSRJumpoutTimer ( GPSRAgent *a, Packet *p ) : TimerHandler (), ttl_seqno_(0), p_(p), a_(a) , status_ (1) 
{ }

GPSRJumpoutTimer::~GPSRJumpoutTimer ()
{  resched(INFINITE_DELAY) ;  }
void 
GPSRJumpoutTimer::expire ( Event  *e)
{
  sendrequest (); 
}

void 
GPSRJumpoutTimer::sendrequest () 
{
  if ( status != 1 )                                                 //  计时器没有效
    return ; 
   if ( ttl_seqno_ < 4 )
    {
      dst_list *dstgroup ; 
      GPSRAgent::getDstgroup ( p_ ,   dstgroup  ); 
      nsaddr_t dst; 
      
      if (  dstgroup ->empty() ) { 
	// 只有 data包有寻找地址阻塞
	// 发送寻找地址数据包
	if ( ttl_seqno_ == 0 ) {
	  dst = dstgroup ->storage_[0].id_; 
	  a_->request ( dst, GPSR_RQTYPE_DST, IP_DEF_TTL  ); 
	} else if ( ttl_seqno_ == 2 ) {
	  // 不再重复发送地址请求数据包 2周期后 标志没有效
	  status = 0 ; 
	  return ; 
	}  // else 
      } else {
	//  跳出路由阻塞
	dst = dstgroup ->storage_[ dstgroup->num_ -1 ].id_ ; 
	a_->request ( dst, GPSR_RQTYPE_OUT, ttls_[ttl_seqno_] ); 
      }
      ttl_seqno_ ++; 
      resched( a_->wait_ask_period_ ); 
    }
   // time out ，sign it
    status = 0 ; 
}


bool packetCompare ( keepTimePacket *ktp, nsaddr_t  dst )
{
  struct hdr_ip *iph = HDR_IP (ktp->p_);
  dst_list *dstgroup;  
  GPSRAgent::getDstgroup ( ktp->p_, dstgroup ); 
  if ( dstgroup->empty (  ) && iph->daddr()  == dst  || dstgroup->storage_[dstgroup->num_ - 1 ] == dst ) {
    return true; 
  } // if 
  return false ; 
}




///////////////////////////////////////
///// bufferqueue 
bufferqueue::bufferqueue ( GPSRAgent  *const a ): a_(a) 
{}

inline void 
bufferqueue::add ( Packet *p ) ; 
{
  bq_.push_back ( keepTimePacket( a_, p ) );
  list<keepTimePacket>::reverse_iterator ktp = bq_.rbegin();
  // 调用请求 开始计时
  ktp ->t_.sendrequest (); 
}


list<Packet*>
bufferqueue::pop ( nsaddr_t dst )
{
  if ( ! size() )
    return list<Packet*>();
 
  list<keepTimePacket*>::iterator it = bq_.begin();
  list<keepTimePacket*>::iterator end = bq_.end();

  list<Packet*>  result;
  while ( it != end )
    {
      struct hdr_ip *iph = HDR_IP ( it->p_ );
      dst_list *dstgroup;  
      GPSRAgent::getDstgroup ( it->p_, dstgroup ); 
      if ( dstgroup->empty (  ) && iph->daddr()  == dst  || dstgroup->storage_[dstgroup->num_ - 1 ] == dst ) {
	// 空白的
	result.push_back ( it->p_ );
	// stop timing 
	it->t_.resched(INFINITE_DELAY);
	bq_.erase ( it ); 
      } 
      ++ it ;
    }
  return result ;
}


void 
bufferqueue::delAllTimeout ( double useful_period )
{
  list<keepTimePacket>::iterator it = bq_.begin();
  list<keepTimePacket>::iterator  end = bq_.end(); 

  while ( it != end ) {
    if ( !  it->t_.isUseful() ) {
      // 计时器失效 数据包去掉
      // 
      struct hdr_ip *iph = HDR_IP ( it->p_ );
      dst_list *dstgroup;  
      GPSRAgent::getDstgroup ( it->p_, dstgroup ); 
      nsaddr_t dst ; 
      if (  dstgroup ->empty() ) { 
	dst = dstgroup ->storage_[0].id_; 
      } else {
	dst = dstgroup ->storage_[ dstgroup->num_ -1 ].id_ ; 
      } // else 
      // 放到cdtable里面，标记没有，以免多次广播不存在的节点
      a_->coortable->add ( cdtable::makeNNEntry(dst) ); 
      bq_.erase ( it ); 
    }
   ++ it ; 
  }
  

}


#endif 
