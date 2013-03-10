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
  if ( status != 1 )                                                 //  ��ʱ��û��Ч
    return ; 
   if ( ttl_seqno_ < 4 )
    {
      dst_list *dstgroup ; 
      GPSRAgent::getDstgroup ( p_ ,   dstgroup  ); 
      nsaddr_t dst; 
      
      if (  dstgroup ->empty() ) { 
	// ֻ�� data����Ѱ�ҵ�ַ����
	// ����Ѱ�ҵ�ַ���ݰ�
	if ( ttl_seqno_ == 0 ) {
	  dst = dstgroup ->storage_[0].id_; 
	  a_->request ( dst, GPSR_RQTYPE_DST, IP_DEF_TTL  ); 
	} else if ( ttl_seqno_ == 2 ) {
	  // �����ظ����͵�ַ�������ݰ� 2���ں� ��־û��Ч
	  status = 0 ; 
	  return ; 
	}  // else 
      } else {
	//  ����·������
	dst = dstgroup ->storage_[ dstgroup->num_ -1 ].id_ ; 
	a_->request ( dst, GPSR_RQTYPE_OUT, ttls_[ttl_seqno_] ); 
      }
      ttl_seqno_ ++; 
      resched( a_->wait_ask_period_ ); 
    }
   // time out ��sign it
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
  // �������� ��ʼ��ʱ
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
	// �հ׵�
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
      // ��ʱ��ʧЧ ���ݰ�ȥ��
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
      // �ŵ�cdtable���棬���û�У������ι㲥�����ڵĽڵ�
      a_->coortable->add ( cdtable::makeNNEntry(dst) ); 
      bq_.erase ( it ); 
    }
   ++ it ; 
  }
  

}


#endif 