/* -*- Mode:C++; c-basic-offset: 2; tab-width:2, indent-tabs-width:t -*- 
 * Copyright (C) 2005 State University of New York, at Binghamton
 * All rights reserved.
 *
 * NOTICE: This software is provided "as is", without any warranty,
 * including any implied warranty for merchantability or fitness for a
 * particular purpose.  Under no circumstances shall SUNY Binghamton
 * or its faculty, staff, students or agents be liable for any use of,
 * misuse of, or inability to use this software, including incidental
 * and consequential damages.

 * License is hereby given to use, modify, and redistribute this
 * software, in whole or in part, for any commercial or non-commercial
 * purpose, provided that the user agrees to the terms of this
 * copyright notice, including disclaimer of warranty, and provided
 * that this copyright notice, including disclaimer of warranty, is
 * preserved in the source code and documentation of anything derived
 * from this software.  Any redistributor of this software or anything
 * derived from this software assumes responsibility for ensuring that
 * any parties to whom such a redistribution is made are fully aware of
 * the terms of this license and disclaimer.
 *
 * Author: Ke Liu, CS Dept., State University of New York, at Binghamton 
 * October, 2005
 *
 * GPSR code for NS2 version 2.26 or later
 * Note: this implementation of GPSR is different from its original 
 *       version wich implemented by Brad Karp, Harvard Univ. 1999
 *       It is not guaranteed precise implementation of the GPSR design
 */

/* gpsr.cc : the definition of the gpsr routing agent class
 *           
 */
#include "gpsr.h"
// #include <functional>

// using std::bind2nd ;




#define DEFAULT_SAFE_DISTANCE 90
int hdr_gpsr::offset_;

static class GPSRHeaderClass : public PacketHeaderClass{
public:
  GPSRHeaderClass() : PacketHeaderClass("PacketHeader/GPSR",
					 sizeof(hdr_all_gpsr)){
    bind_offset(&hdr_gpsr::offset_);
  }
}class_gpsrhdr;

static class GPSRAgentClass : public TclClass {
public:
  GPSRAgentClass() : TclClass("Agent/GPSR"){}
  TclObject *create(int, const char*const*){
    return (new GPSRAgent());
  }
}class_gpsr;



////////////////////////////////////////////////
// Timer setting
void
GPSRHelloTimer::expire(Event *e){
  // flag个周期清理数据库
  if ( (counter_ % modulus_ ) == 0 )
    a_->nblist_->delAllTimeout ( modulus_ *a_->hello_period_ ); 
  a_->hellotout();
  counter_ ++; 
}

void
CDTableTimer::expire(Event *e){
  a_->coortable_->delAllTimeout  (  a_->clean_cdtable_period_ ); 
  resched(   a_->clean_cdtable_period_   ); 
}

void
HelpOutDataTimer::expire(Event *e){
  a_->helplist_ ->delAllTimeout  (  a_->clean_helpoutdata_period_ ); 
  resched(   a_->clean_helpoutdata_period_   ); 
}

void
BufferQueueTimer::expire(Event *e){
  a_->bufferq_->delAllTimeout  (  a_->clean_bufferqueue_period_ ); 
  resched(   a_->clean_bufferqueue_period_ ); 
}

unsigned 
GPSRJumpoutTimer::ttls_[4] = { 4, 8,  IP_DEF_TTL/2,  IP_DEF_TTL }; 

GPSRJumpoutTimer::GPSRJumpoutTimer ( GPSRAgent *a, Packet *p ) : TimerHandler (), ttl_seqno_(0), p_(p), a_(a) , status_ (1) 
{ }

GPSRJumpoutTimer::~GPSRJumpoutTimer ()
{  
  force_cancel ();                                        // it is importance , take the item from the schedule list. 
  a_ = NULL;
  p_ = NULL; 
}

void 
GPSRJumpoutTimer::expire ( Event  *e)
{
  sendrequest (); 
}

void 
GPSRJumpoutTimer::sendrequest () 
{
  if ( status_ != 1 )                                                 //  计时器没有效
    return ; 
   if ( ttl_seqno_ < 4 )
    {
      dst_list *dstgroup ; 
      if ( ! p_ )  assert ( false ); 
      GPSRAgent::getDstgroup ( p_ ,   dstgroup  ); 
      node_info  dst; 
      
      if (  dstgroup ->empty() ) { 
	// 只有 data包有寻找地址阻塞
	// 发送寻找地址数据包

	dst = dstgroup ->storage_[0]; 
	a_->request ( dst, GPSR_RQTYPE_DST, IP_DEF_TTL  ); 

      } else {
	//  跳出路由阻塞
	dst = dstgroup ->storage_[ dstgroup->num_ -1 ]; 
	a_->request ( dst, GPSR_RQTYPE_OUT, ttls_[ttl_seqno_] ); 
      }
      ttl_seqno_ ++; 
      resched( a_->wait_ask_period_ ); 
    }
   // time out ，sign it
   else 
     status_ = 0 ; 
}

// end time setting 
/////////////////////////////////////////////////
// buffer queue define 
// bool packetCompare ( keepTimePacket *ktp, nsaddr_t  dst )
// {
//   struct hdr_ip *iph = HDR_IP (ktp->p_);
//   dst_list *dstgroup;  
//   GPSRAgent::getDstgroup ( ktp->p_, dstgroup ); 
//   if ( dstgroup->empty (  ) && iph->daddr()  == dst  || dstgroup->storage_[dstgroup->num_ - 1 ].id_  == dst ) {
//     return true; 
//   } // if 
//   return false ; 
// }

bufferqueue::bufferqueue ( GPSRAgent  *const a ): a_(a) , bq_(NULL),  packet_num_(0)
{}


bufferqueue::~bufferqueue () 
{
  delAll (); 
}

void 
bufferqueue::delAll () 
{
  struct keepTimePacket* temp = bq_ ;
  struct keepTimePacket *dd = NULL;
  while ( temp ) {
    struct keepTimePacket *ddgroup = temp ;
    temp = temp ->next ; 
    while ( ddgroup  ) { 
      struct keepTimePacket *dd = ddgroup;
      ddgroup = ddgroup->samedstp; 
      Packet::free( dd->p_) ; 
      delete dd; 
    } // while  2
 
  } // while 

  bq_ = NULL;
  packet_num_ = 0; 
}

bool  
bufferqueue::add ( Packet *p ) 
{
  keepTimePacket *new_item = NULL; 
  new_item = new keepTimePacket ( a_, p ) ; 
  if ( ! new_item )
    assert ( false ); 
  packet_num_ ++; 
 
  dst_list *dstgroup;  
  GPSRAgent::getDstgroup ( p, dstgroup ); 
  nsaddr_t new_dst = dstgroup->nextdst().id_;


  if ( ! bq_) {
    //buffer queue 第一个数据包
    new_item->next =  bq_; 
    bq_ = new_item ; 
    new_item ->t_.sendrequest ();                                               // 相同目的地址 第一个数据包发送请求
    return true;
  } else {
    //寻找是否有相同目的地址的数据包在等待
    keepTimePacket *temp = bq_; 
    while ( temp ) {
      GPSRAgent::getDstgroup ( temp->p_, dstgroup ); 
      nsaddr_t dst = dstgroup->nextdst().id_;
      if ( new_dst == dst ) {
	break ; 
      }
      temp = temp->next; 
    } // while 

    if ( ! temp ) {
      // 找不到相同目的地址的数据包
      // 在头部插入该数据包
      new_item->next =  bq_; 
      bq_ = new_item ; 
      new_item ->t_.sendrequest ();                                               // 相同目的地址 第一个数据包发送请求
      return true;
    } else {
      // 有相同目的地址数据包
      new_item->samedstp = temp->samedstp; 
      temp->samedstp = new_item; 
      return false; 
    }  // else 2 
  }  // else 

}


keepTimePacket *
bufferqueue::pop ( nsaddr_t dst )
{
  if ( ! bq_  )
    return NULL; 
  struct keepTimePacket *  temp  = bq_ ;
  struct keepTimePacket *  pre =  NULL;
  struct keepTimePacket * result = NULL; 
  while ( temp  )
    {
      struct hdr_ip *iph = HDR_IP ( temp->p_ );
      dst_list *dstgroup;  
      GPSRAgent::getDstgroup ( temp->p_, dstgroup ); 
      nsaddr_t dst_in_buffer = dstgroup->nextdst().id_;
      if ( dst == dst_in_buffer ) { 
	break;
      }
      pre = temp;
      temp = temp->next; 
    } //while
  if ( ! temp  ) { return NULL; }
  // 移除组
  if ( !  pre ) {
    bq_ = temp->next; 
  } else {
    pre->next = temp->next; 
  }
  
  result = temp ;
  while ( temp ) {
    // 处理相同移除数据包
    temp->t_.resched( INFINITE_DELAY );
    -- packet_num_; 
    temp = temp->samedstp;                                   // 垂直移动
  }
  return result ;
}

void 
bufferqueue::delAllTimeout ( double useful_period )
{

  if ( ! bq_ ) return ; 
  struct keepTimePacket *  temp  = bq_ ;
  struct keepTimePacket *  pre = NULL ; 
  struct keepTimePacket * ddgroup = NULL;
 

  while ( temp ) {
    if ( !  temp->t_.isUseful() ) {
      // 计时器失效 数据包组去掉
      ddgroup = temp;
      // 移动到下一跳
      if ( ! pre ) {
	// 第一项
	bq_ = temp->next; 
	temp = temp->next; 
      } else {
	pre->next = temp->next; 
	temp = pre->next; 
      }  // else 2 

      // 放到cdtable里面，标记没有，以免多次广播不存在的节点
      dst_list *dstgroup;  
      GPSRAgent::getDstgroup ( ddgroup->p_, dstgroup ); 
      nsaddr_t dst = dstgroup->nextdst().id_;
      a_->addCDtableItem ( cdtable::makeNNEntry(dst) ); 

      // 处理需要删除组
      struct keepTimePacket * dd = ddgroup; 
      while ( ddgroup ) {
	dd = ddgroup;
	ddgroup = ddgroup->samedstp; 
	Packet::free ( dd->p_ ) ; 
	delete dd;
	packet_num_ -- ; 
      }  // while 
      ddgroup = NULL; 
    } else {
      // 计时器有效
      pre = temp; 
      temp = temp->next; 
    } // else 
  }  // while 

}


////////////////////////////////////////////////
void
GPSRAgent::hellotout(){
  // 更新agent地址信息 
  getLoc();
  hellomsg();
  hello_timer_.resched(hello_period_);
  // 更新neighbor 地址信息
  nblist_->myinfo ( myinfo () ); 
}

void
GPSRAgent::getLoc(){
  double pos_z_;
  node_->getLoc(&my_x_, &my_y_, &pos_z_);
}

void
GPSRAgent::initTimer ()
{
  clean_cdtable_timer_.resched( clean_cdtable_period_ );
  clean_helpoutdata_timer_.resched ( clean_helpoutdata_period_ );
  clean_bufferqueue_timer_.resched ( clean_bufferqueue_period_ ); 
}


GPSRAgent::GPSRAgent() : Agent(PT_GPSR), 
                         hello_timer_(this),  clean_cdtable_timer_(this),  
			 clean_helpoutdata_timer_(this),  clean_bufferqueue_timer_(this),
                         my_id_(-1), my_x_(0.0), my_y_(0.0),
			 requestAskCounter_(0),  on_gpsr_status_(false)
{
  bind("hello_period_",  &hello_period_ );
 
  bind ("clean_helpoutdata_period_", &clean_helpoutdata_period_  );
  bind ("clean_bufferqueue_period_", &clean_bufferqueue_period_ );
  bind ( "wait_ask_period_",  &wait_ask_period_ );
  bind ( "clean_hello_modulus_", &clean_hello_modulus_ ) ; 

  bind ( "gpsr_safe_distance_", &safe_distance_ ) ; 

 // 不知道为何clean_cdtable_period_ 设置不了
  //  bind(" clean_cdtable_period_",  &clean_cdtable_period_  );

  clean_cdtable_period_ =  clean_helpoutdata_period_ ;


  // if ( clean_cdtable_period_ == 0 )
  //   clean_cdtable_period_ = 3; 
  // 初始化各个计时器
  // initTimer (); 
  
  //  初始化各个数据库 
  coortable_ = new cdtable ;
  nblist_ = new GPSRNeighbors;
  nblist_->setSafedistance( safe_distance_ );
  helplist_ = new helpoutdata ; 
  bufferq_ = new bufferqueue( this ); 
  for(int i=0; i<5; i++)
    randSend_.reset_next_substream();
}


//modify by anzizhao
//添加析构函数
GPSRAgent::~GPSRAgent()
{
  delete coortable_;
  delete nblist_;
  delete helplist_; 
  delete bufferq_; 
}

bool
GPSRAgent::isGPSROn() const 
{  return on_gpsr_status_; }

void
GPSRAgent::turnon(){
  getLoc();
  nblist_->myinfo( myinfo() );
  hello_timer_.setModulus( clean_hello_modulus_ ); 
  hello_timer_.resched( randSend_.uniform(0.0, 0.5) );
  initTimer(); 
  on_gpsr_status_ = true; 
}
// 关闭意义：
// 1. 从gpsr网络退出
// 2. 关闭自身信息发送gpsr，继续接受监听gpsr网络信息，其他节点发送数据接受不到
// 这里选择原来设定的2。
void
GPSRAgent::turnoff(){
  hello_timer_.resched( INFINITE_DELAY );
  clean_cdtable_timer_.resched(  INFINITE_DELAY );
  clean_helpoutdata_timer_.resched (  INFINITE_DELAY );
  clean_bufferqueue_timer_.resched (  INFINITE_DELAY );

  bufferq_->delAll (); 

  on_gpsr_status_ = false; 
}

node_info 
GPSRAgent::myinfo ( )
{
  node_info node;
  node.id_ = my_id_;
  node.x_ = my_x_;
  node.y_  = my_y_;
  return node;
}


void 
GPSRAgent::hellomsg(){
 
  Packet *p = allocpkt();
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_hello *ghh = HDR_GPSR_HELLO(p);

  cmh->next_hop_ = IP_BROADCAST;
  cmh->last_hop_ = my_id_;
  cmh->addr_type_ = NS_AF_INET;
  cmh->ptype() = PT_GPSR;
  cmh->direction() = hdr_cmn::DOWN;

  iph->daddr() = IP_BROADCAST;
  iph->saddr() = my_id_;
  iph->sport() = RT_PORT;
  iph->dport() = RT_PORT;
  iph->ttl_ = 1 ;

  ghh->type_ = GPSRTYPE_HELLO;
  ghh->node_ = myinfo(); 
  ghh->ts_ = (float) GPSR_CURRENT;

  send(p, 0);
}




void
GPSRAgent::recvHello(Packet *p){
  struct hdr_gpsr_hello *ghh = HDR_GPSR_HELLO(p);
  gpsr_neighbor gn; 
  gn.node_ = ghh->node_;
  gn.ts_      = ghh->ts_; 
  nblist_->add ( gn ); 
  Packet::free(p);
}

//////////////////////////////////////////////////
// 发送请求的数据包
void 
GPSRAgent::request ( const node_info &  dst , u_int8_t type  , int ttl  )
{
  Packet *p = allocpkt();

  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_query *gqh = HDR_GPSR_QUERY_REQUEST(p);

  iph->daddr() = dst.id_ ;
  // 记录源节点id
  iph->saddr() = my_id_;
  iph->sport() = RT_PORT;
  iph->dport() = RT_PORT;

  iph->ttl_ = ttl ;

  gqh->type_ = GPSRTYPE_QUERY_REQUEST  ;
  gqh->query_type_ = type ;                                           // 请求的类型

  gqh->dst_   = dst;                                                    // dst 跳出空洞时 坐标是目的地址 否则为0


  gqh->hops_  = 1;
  gqh->base_.node_ = myinfo(); 
  gqh->base_.seqno_ = requestAskCounter_++;
  gqh->base_.ts_    =  (float)GPSR_CURRENT;
  
  cmh->direction() = hdr_cmn::DOWN;
  cmh->next_hop_ = IP_BROADCAST;
  cmh->last_hop_ = my_id_;
  cmh->addr_type_ = NS_AF_INET;
  cmh->ptype() = PT_GPSR;

  cmh->size() = IP_HDR_LEN + gqh->size();
  send( p , 0);
}

#define IP_NONE  -1
// 用于查找下一跳
nsaddr_t 
GPSRAgent::_findNexthop ( Packet *p , dst_list & dsts ,  int loop_num )
{
  //  struct hdr_cmn *cmh = HDR_CMN(p);
  if ( !  loop_num ){
       bufferq_->add ( p );
    // Packet::free ( p  ); 
    return IP_NONE; 
  }
  node_info me = myinfo (); ; 
  node_info  *np = NULL  ; 
  hdr_gpsr_askdata_common gadh (p );
 
  node_info & src = gadh.Srcgroup().nextdst(); 
  node_info&  dst = dsts.nextdst();
  nsaddr_t result = IP_NONE; 
  // 判断最新地址是否存在路径到达
  if (  ! nblist_-> shortestDistance ( src   ,  dst  , np ) )
    {
      struct helpoutentry *hp = NULL; 
      if (  ! helplist_->getEntrybyID( dst.id_ , hp ) )                     //不存在辅佐路径
	{
	  // 加入缓冲区后会发送请求数据包
	
	  gadh.Mode() = GPSR_TFTYPE_PF;
	  //	  gadh.Srcgroup().add( me ); 
	  bufferq_->add ( p );
	  result = IP_NONE;
	} // if 2 
      else                                                                               // 存在辅佐路径
	{
	  struct cooentry *ep = NULL ;
	  if ( ! coortable_->getEntrybyID ( hp->shouldgo_ , ep  ) ) 
	    assert ( false );                                                // 找到邻居节点

	  if ( *ep == cdtable::makeNNEntry(ep->node_.id_) ) {   // 判断是否之前广播过，不存在的节点
	    // 是 删除数据包  本应不存在
#ifdef  _AN_DEBUG
	    FILE *fp = fopen( "node_not_in_gpsr.txt", "a+" );
	    fprintf ( fp , "%2.f\t%d\t%d\n", GPSR_CURRENT, my_id_, ep->node_.id_ ); 
	    fclose ( fp ); 
#endif 
	    Packet::free ( p ); 
	    return IP_NONE ; 
	  }
	  gadh.Srcgroup().add( me ); 
	  dsts.add ( ep->node_ ); 
	
#ifdef  _AN_DEBUG
	  // debug 使用代码
	  double current_time = (float)GPSR_CURRENT;
	  if ( current_time > 80  ) {
	    FILE *fp = fopen( "findnexthop.txt", "a+" );
	    struct hdr_cmn *cmh = HDR_CMN(p);
	    fprintf  ( fp, "%2.f\t%d\t%d\t%d\t%2.f\t%2.f\t%d\n", current_time, my_id_, cmh->uid_,  ep->node_.id_, ep->node_.x_,
		       ep->node_.y_, ep->seqno_ ); 
	    fclose ( fp ); 
	  }
#endif 
	  free ( ep ); 
	  result  =   _findNexthop ( p , dsts , --loop_num ) ; 
	}
      free ( hp );
    }
  else                                                                                  // 找到可到达目的的下一条
    { 
      
      result  = np->id_ ; 
      free ( np ); 
    }
 
  return result ;
}

void 
GPSRAgent::_fillDsts( Packet *p )
{
  hdr_gpsr_askdata_common gadh( p ); 
   nsaddr_t nexthop ; 
   nexthop = _findNexthop (p, gadh.Dstgroup()  );

   if ( nexthop  != IP_NONE ) {
     struct hdr_cmn *cmh = HDR_CMN(p);
     cmh->direction() = hdr_cmn::DOWN;

     // the most important item 
     cmh->next_hop_ = nexthop ;
     // 记录上一跳节点
     cmh->last_hop_ = my_id_;
     cmh->addr_type_ = NS_AF_INET;
     cmh->ptype() = PT_GPSR;
     // 发送
     send ( p ,  0 ); 
   } 

}

u_int8_t 
GPSRAgent::getDstgroup ( Packet *p ,  dst_list *& dstgroup ) 
{
  hdr_gpsr_askdata_common gadh(p ); 
  dstgroup = & ( gadh.Dstgroup() ); 
  return gadh.Type(); 
 }

// 以info信息 应答 dst
//调用者知道是那种请求类型
void 
GPSRAgent::ask ( Packet *&ori_packet,  const struct packet_info & info )
{
  struct hdr_gpsr_query  *ori_gqh =  HDR_GPSR_QUERY_REQUEST(ori_packet);
  node_info dst = ori_gqh -> base_.node_ ; 
  u_int8_t query_type = ori_gqh->query_type_; 
  nsaddr_t  ask_for_id = ori_gqh->dst_.id_ ; 
  Packet::free( ori_packet ); 
  ori_packet = NULL; 

  Packet *p = allocpkt();
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_ask *gah = HDR_GPSR_QUERY_ASK(p);
  struct hdr_cmn *cmh = HDR_CMN(p);
  
  cmh->size() = IP_HDR_LEN + gah->size();
  
  iph->daddr() = dst.id_ ;
  // 记录源节点id
  iph->saddr() = my_id_;
  iph->sport() = RT_PORT;
  iph->dport() = RT_PORT;
  iph->ttl_ = IP_DEF_TTL;

  gah->type_ = GPSRTYPE_QUERY_ASK; 
  gah->mode_ = GPSR_TFTYPE_GF;                               // 传输模式
  gah->query_type_ = query_type ;
  gah->hops_  = 1; 
  gah->ask_for_id_ = ask_for_id ;
  gah->ask_info_ = info ; 
  gah->base_.node_ = myinfo();
  gah->base_.seqno_ = requestAskCounter_ ++ ; 
  gah->base_.ts_  =  (float)GPSR_CURRENT; 
  
  gah->dstgroup_.storage_[0] = dst; 
  gah->dstgroup_.num_  = 1;                                    //  应答知道对方地址
  
  gah->srcgroup_.num_ = 1;
  gah->srcgroup_.storage_[0] = myinfo();

  _fillDsts ( p )   ;                                                         // 填充目的地址信息，寻找下一跳

}

void 
GPSRAgent::data ( Packet *p ) 
{
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_data *gdh =  HDR_GPSR_DATA(p);
  node_info me = myinfo (); 
 
  cmh->size() +=  IP_HDR_LEN + gdh->size();

  gdh->type_ = GPSRTYPE_DATA;
  gdh->mode_ =  GPSR_TFTYPE_GF ;                // 传输模式 
  gdh->base_.node_ = myinfo (); 

  gdh->dstgroup_.num_ = 0;                        //   判断是否 emtpy 用
  node_info dst;
  dst.id_ = iph->daddr();
  dst.x_ = 0.0;
  dst.y_  = 0.0;
  gdh->dstgroup_.storage_[0] = dst ; 

  gdh->srcgroup_.num_ = 0;
  gdh->srcgroup_.storage_[0] = myinfo(); 
  
  cooentry * dinfo = NULL ;
  if ( coortable_->getEntrybyID ( iph->daddr(), dinfo ) )         // 查询目的坐标
    {
      if ( *dinfo == cdtable::makeNNEntry(dinfo->node_.id_) ) {   // 判断是否之前广播过，不存在的节点
	// 是 删除数据包
#ifdef  _AN_DEBUG
	FILE *fp = fopen( "node_not_in_gpsr.txt", "a+" );
	fprintf ( fp , "%2.f\t%d\t%d\n", GPSR_CURRENT, my_id_, dinfo->node_.id_ ); 
	fclose ( fp ); 
#endif 
	Packet::free ( p ); 
	return ; 
      }
      gdh->srcgroup_.add ( me ) ; 
      gdh->dstgroup_.add( dinfo->node_); 
      _fillDsts ( p );                                            // 就像ask一样 交给_fillDsts 处理后面
      free ( dinfo ); 
    }  // if 
  else                                                                  // 查询不到目的坐标
    {
      bufferq_->add ( p );                                  // 加入缓冲队列                      
    } // else 
  

  
} // e f 

void
GPSRAgent::recv(Packet *p, Handler *h){
  //  节点没哟开始 将数据包
  if (  !  isGPSROn() ) {
    Packet::free ( p  ); 
    return ;
  }
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);

  // 判断数据包传递方向 ， 上层 接受？ 下层 发送？
  if( cmh->ptype() != PT_GPSR )
    {
      // 上层数据 , 还没有填写gpsr类型信息
      if (  iph->saddr() == my_id_  && cmh->num_forwards() == 0   ) {  
	// a packet genernate by myself ， fill the packet with gpsr info and send it .
	data ( p ) ;                                     // 
      } 
      //   drop(p, DROP_RTR_ROUTE_LOOP);
      else {
	// 或是另一种路由协议
	Packet::free(p);
      }
      return ; 
    }

 
 // 往下层发送 只有gpsr data类型
  struct hdr_gpsr *gh = HDR_GPSR(p);

 
   switch(gh->type_){
  case GPSRTYPE_HELLO:
    recvHello(p);
    break;
  case GPSRTYPE_QUERY_REQUEST :
    if (   iph->saddr() == my_id_  )                 // 如果是GPSRTYPE_QUERY_REQUEST数据包，是洪泛广播，所以防止收到自己的
      {
	Packet::free(p);
	return ; 
      }
    recvQueryRequest ( p );
    break;
  case GPSRTYPE_QUERY_ASK :
    recvQueryAsk(p);
    break;
  case  GPSRTYPE_DATA :
    recvData ( p ); 
    break;
  default:
    printf("Error with gf packet type.\n");
    exit(1);
  }   // switch 
 
  
}  // e f



// 接受到请求的数据包 
void
GPSRAgent::recvQueryRequest(Packet *p){
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_query *gqh = HDR_GPSR_QUERY_REQUEST(p);
  struct packet_info  pinfo;

  // 判断是否数据包信息是否有效
  // 更新源节点信息，  失败 已经处理过该数据包
  if ( ! coortable_->add ( gqh->base_, cmh->last_hop_, gqh->hops_ ) )
    {
      Packet::free(p);
      return ;
    }
  // 查询信息  有，应答   没有，继续广播

  if ( gqh->query_type_ == GPSR_RQTYPE_DST )                       //  请求目的地址
    {
      cooentry *dstp = NULL ;
    
      if ( gqh->dst_.id_  ==  my_id_ )                                                  // 自己是请求地址
	{
	  pinfo.node_ = myinfo();
	  pinfo.seqno_ = requestAskCounter_ ++;
	  pinfo.ts_     = (float)GPSR_CURRENT; 
	  ask ( p,  pinfo );  

	  return ;
	}
      // 查询邻居表  有 单播发送
      if ( nblist_->isEntry ( gqh->dst_.id_) )
	{
	  cmh->next_hop_ =gqh->dst_.id_ ;
	}
      // 查询cdtable  是否有效的目的节点信息
      else  if  ( coortable_->getEntrybyID(gqh->dst_.id_ , dstp )  )      
	{
	  pinfo.node_ = dstp->node_;
	  pinfo.seqno_  = dstp->seqno_;
	  pinfo.ts_        = dstp->ts_;
	  ask ( p ,  pinfo );  
	  free ( dstp );
	
	  return ;
        	}
     
   
    }
  else if (   gqh->query_type_ == GPSR_RQTYPE_OUT && gqh->dst_.id_ != my_id_   )            // 请求跳出空洞  目标节点不是该节点
    {
      // 和自己比较
      if ( gqh->base_.node_.id_ != my_id_ )
	{
	  node_info  me = myinfo(); 
	  double mydis = GPSRNeighbors::getdis ( me , gqh->dst_ );
	  double oridis =  GPSRNeighbors::getdis ( gqh->base_.node_, gqh->dst_ );
	  // 自己符合要求
	  if ( mydis < oridis )                                        
	    {
	      
	      pinfo.node_ =  me ;
	      pinfo.seqno_ = requestAskCounter_++;
	      pinfo.ts_  = (float) GPSR_CURRENT;
	      ask ( p, pinfo  );
	  
	      return ;
	    }
	}  // if 2 
      // 查询邻居表   
      // 是否存在比src-dst更短的距离  有 单播发送 ，没有  继续广播
      struct node_info  *np = NULL  ; 
      if ( nblist_-> shortestDistance ( gqh->base_.node_ , gqh->dst_, np ) )
	{
	  cmh->next_hop_ = np->id_; 
	  iph->daddr()   = np->id_; 
	  free ( np );
	}   // if 2 

    
    } //  else if 1 
  
  cmh->direction() = hdr_cmn::DOWN;
  cmh->last_hop_ = my_id_; 
  if (  ! (--iph->ttl_)  )  {
      Packet::free(p);
      return ;
    }
  //  cmh->size() = IP_HDR_LEN + gqh->size();
  //  
  send( p, 0);
}


//  接受应答的数据包
void
GPSRAgent::recvQueryAsk (Packet *p){
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_ask *gah = HDR_GPSR_QUERY_ASK(p);

  // 以src 和 ask info 更新cdtable坐标
  coortable_->add ( gah->base_, cmh->last_hop_,  gah->hops_ );  
    
  coortable_->add ( gah->ask_info_, cmh->last_hop_,  gah->hops_ );

 
  nsaddr_t nexthop = IP_NONE;
  // 判断该节点 中间节点？ 目的节点？
  if ( my_id_ != gah->dstgroup_.top().id_ ) 
    {
      // 更新比较地址
      gah->srcgroup_.nextdst() = myinfo(); 
    }
  else 
    {
      gah->dstgroup_.pop();
      gah->srcgroup_.pop();   
      if (  gah->dstgroup_.empty()  )
	{
	  // 最终目的节点
	  dealwithAsk ( p ) ; 
	  return ; 
	}
    }
  
 // 不是目的节点 和中间节点 处理操作
  nexthop = _findNexthop ( p, gah->dstgroup_ ); 
  // 存在下一跳 ,发送， 否则 不处理 ， 上函数已经发送请求
  if ( nexthop == IP_NONE )  return ;          
  if ( ! ( -- iph->ttl_)  )  {
    Packet::free(p);
    return ;
  }                     
  cmh->direction() = hdr_cmn::DOWN;
  cmh->next_hop_ = nexthop ; 
  gah->hops_ ++; 
  cmh->last_hop_ = my_id_; 
  send ( p , 0); 


 }// end 

void
GPSRAgent::dealwithAsk ( Packet *p )
{
   struct hdr_gpsr_ask *gah = HDR_GPSR_QUERY_ASK(p);
    // 判断请求类型  如果是跳出空洞  更新辅佐 数据库信息
  if ( gah->query_type_ == GPSR_RQTYPE_OUT  )
    {
      helpoutentry  hoentry ;
      hoentry.wheretogo_ = gah->ask_for_id_ ; 
      hoentry.shouldgo_   = gah->ask_info_.node_.id_; 
      hoentry.ts_   = gah->ask_info_.ts_;
      hoentry.hops_ =  gah->hops_; 
	
      helplist_->add ( hoentry ); 
	
    } else  {
    //    寻找地址
    // do nothing now .  
  } // else 
    // 缓冲区相应数据包发送
  sendBufferPacket ( gah->ask_for_id_ );                                  // the most important thing to do 

  Packet::free ( p ); 
}

void
GPSRAgent::recvData ( Packet *p )
{
  struct hdr_cmn *cmh = HDR_CMN(p);
  struct hdr_ip *iph = HDR_IP(p);
  struct hdr_gpsr_data *gdh = HDR_GPSR_DATA(p);
  
  // 更新关于src的cdtable
  coortable_->add ( gdh->base_, cmh->last_hop_,  gdh->hops_ );
  
  
  nsaddr_t nexthop = IP_NONE;
  // 可能是中间节点 或目的节点

  // 是否是一个目的节点
  if ( my_id_ != gdh->dstgroup_.top().id_ ) 
    {
      // 不是一个目的节点
      // 寻找下一跳
      //   gdh->srcgroup_.storage_[1] = myinfo(); 
      gdh->srcgroup_.nextdst() = myinfo();

    }
  else 
    {
        // 是一个目的节点
      gdh->dstgroup_.pop();
      gdh->srcgroup_.pop();   
      if (  gdh->dstgroup_.empty()  )
	{
	  // 最终目的节点
	  send ( p , 0 ) ; 
	  return ; 
	}
      gdh->mode_ = GPSR_TFTYPE_GF;
      
      // 中间节点
    } // else 1 
  // 不是目的节点 和中间节点 处理操作
  nexthop = _findNexthop ( p, gdh->dstgroup_ ); 
  // 存在下一跳 ,发送， 否则 不处理 ， 上函数已经发送请求
  if ( nexthop == IP_NONE )  return ;                               
  cmh->direction() = hdr_cmn::DOWN;
  cmh->next_hop_ = nexthop ; 
  gdh->hops_ ++; 
  cmh->last_hop_ = my_id_; 
  // 填写信息
  if ( ! ( -- iph->ttl_)  )  {
    Packet::free(p);
    return ;
  }
  send ( p , 0); 
}

void 
GPSRAgent::sendBufferPacket ( nsaddr_t id )
{
  keepTimePacket *  ready_packet;
  ready_packet = bufferq_->pop(id); 
  keepTimePacket *  temp = ready_packet; 
  keepTimePacket * dd = NULL; 
  node_info me = myinfo (); 
  while ( temp  ) {
    hdr_gpsr_askdata_common gadh ( temp->p_ ); 
    if ( gadh.Type() == GPSRTYPE_DATA  &&  gadh.Dstgroup().empty()  ) 
      {
	//数据包且没有填写地址n 
	// 查询坐标表
	struct cooentry * dinfo = NULL ;
	nsaddr_t dst = gadh.Dstgroup().storage_[0].id_; 
	if (  coortable_->getEntrybyID ( dst , dinfo ) )
	  {
	    if ( *dinfo == cdtable::makeNNEntry(dinfo->node_.id_) ) {   // 判断是否之前广播过，不存在的节点
	      // 是 删除数据包
#ifdef  _AN_DEBUG
	      FILE *fp = fopen( "node_not_in_gpsr.txt", "a+" );
	      fprintf ( fp , "%2.f\t%d\t%d\n", GPSR_CURRENT, my_id_, dinfo->node_.id_ ); 
	      fclose ( fp ); 
#endif 
	      Packet::free ( temp->p_ ); 
	      return ; 
	    }   
	    gadh.Srcgroup().add ( me ) ; 
	    gadh.Dstgroup().add( dinfo->node_); 
	    gadh.Base().ts_ = ( float ) GPSR_CURRENT; 
	    free ( dinfo ); 
	  } //  if 
	else 
	  assert (false ); 

	_fillDsts ( temp->p_  );                                // 寻找下一跳 发送

      }  // if 
    else 
      {
	//  阻塞的原因肯定是不知道怎样去目的地址  需要中间地址
	//	gadh.Srcgroup().storage_[1] = myinfo(); 
	//	node_info me = myinfo();
	//	gadh.Srcgroup().add ( me ); 
	_fillDsts ( temp->p_ ); 
      }
    dd = temp;
    temp = temp ->samedstp; 
    delete dd; 
  } // while 
    

} // end 
  


void 
GPSRAgent::trace(char *fmt, ...){
  va_list ap;
  if(!tracetarget)
    return;
  va_start(ap, fmt);
  vsprintf(tracetarget->pt_->buffer(), fmt, ap);
  tracetarget->pt_->dump();
  va_end(ap);
}

int
GPSRAgent::command(int argc, const char*const* argv){
  if(argc==2){
    if(strcasecmp(argv[1], "turnon")==0){
      turnon();
      return TCL_OK;
    } else if(strcasecmp(argv[1], "turnoff")==0){
      turnoff();
      return TCL_OK;
    } else if (strcasecmp(argv[1], "neighborlist") == 0) {
      showNeighbor(); 
      return TCL_OK;
    }

  } else if (  argc == 3 ) {

    if(strcasecmp(argv[1], "addr")==0){
      my_id_ = Address::instance().str2addr(argv[2]);
      return TCL_OK;
    }

    TclObject *obj;
    if ((obj = TclObject::lookup (argv[2])) == 0){
      fprintf (stderr, "%s: %s lookup of %s failed\n", __FILE__, argv[1],
	       argv[2]);
      return (TCL_ERROR);
    }
    
    if ( strcasecmp (argv[1], "node") == 0 ) {
      node_ = (MobileNode*) obj;
      return (TCL_OK);
    } else if (strcasecmp (argv[1], "port-dmux")  == 0) {
      port_dmux_ = (PortClassifier*) obj; //(NsObject *) obj;
      return (TCL_OK);
    } else if(strcasecmp(argv[1], "tracetarget") ==0){
      tracetarget = (Trace *)obj;
      return TCL_OK;
    }
  } // else if 1 
 
  return (Agent::command(argc, argv));
}


void 
GPSRAgent::addCDtableItem ( cooentry entry )
{
  coortable_ -> add ( entry ); 
}


void 
GPSRAgent::showNeighbor () const 
{
  nblist_ -> showlist(); 
}


