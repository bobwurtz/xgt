#pragma once
#include <xgt/protocol/block.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace xgt { namespace chain {

   using xgt::protocol::signed_block;
   using xgt::protocol::block_id_type;

   struct fork_item
   {
      private:
         fork_item(){}
      public:
         fork_item( signed_block d )
         :num(d.block_num()),id(d.id()),data( std::move(d) ){}

         block_id_type previous_id()const { return data.previous; }

         weak_ptr< fork_item > prev;
         uint32_t              num;    // initialized in ctor
         /**
          * Used to flag a block as invalid and prevent other blocks from
          * building on top of it.
          */
         bool                  invalid = false;
         block_id_type         id;
         signed_block          data;
   };
   typedef shared_ptr<fork_item> item_ptr;


   /**
    *  As long as blocks are pushed in order the fork
    *  database will maintain a linked tree of all blocks
    *  that branch from the start_block.  The tree will
    *  have a maximum depth of 1024 blocks after which
    *  the database will start lopping off forks.
    *
    *  Every time a block is pushed into the fork DB the
    *  block with the highest block_num will be returned.
    */
   class fork_database
   {
      public:
         typedef vector<item_ptr> branch_type;
         /// The maximum number of blocks that may be skipped in an out-of-order push
         const static int MAX_BLOCK_REORDERING = 1024;

         fork_database();
         void reset();

         void                             start_block(signed_block b);
         void                             remove(block_id_type b);
         void                             set_head(shared_ptr<fork_item> h);
         bool                             is_known_block(const block_id_type& id)const;
         shared_ptr<fork_item>            fetch_block(const block_id_type& id)const;
         vector<item_ptr>                 fetch_block_by_number(uint32_t n)const;

         /**
          *  @return the new head block ( the longest fork )
          */
         shared_ptr<fork_item>            push_block(const signed_block& b);
         shared_ptr<fork_item>            head()const { return _head; }
         void                             pop_block();

         /**
          *  Given two head blocks, return two branches of the fork graph that
          *  end with a common ancestor (same prior block)
          */
         pair< branch_type, branch_type >  fetch_branch_from(block_id_type first,
                                                             block_id_type second)const;
         shared_ptr<fork_item>            walk_main_branch_to_num( uint32_t block_num )const;
         shared_ptr<fork_item>            fetch_block_on_main_branch_by_number( uint32_t block_num )const;

         struct block_id;
         struct block_num;
         struct by_previous;
         typedef boost::multi_index_container<
            item_ptr,
            boost::multi_index::indexed_by<
               boost::multi_index::hashed_unique<boost::multi_index::tag<block_id>, boost::multi_index::member<fork_item, block_id_type, &fork_item::id>, std::hash<fc::ripemd160>>,
               boost::multi_index::hashed_non_unique<boost::multi_index::tag<by_previous>, boost::multi_index::const_mem_fun<fork_item, block_id_type, &fork_item::previous_id>, std::hash<fc::ripemd160>>,
               boost::multi_index::ordered_non_unique<boost::multi_index::tag<block_num>, boost::multi_index::member<fork_item,uint32_t,&fork_item::num>>
            >
         > fork_multi_index_type;

         void set_max_size( uint32_t s );

      private:
         /** @return a pointer to the newly pushed item */
         void _push_block(const item_ptr& b );
         void _push_next(const item_ptr& newly_inserted);

         uint32_t                 _max_size = 1024;

         fork_multi_index_type    _unlinked_index;
         fork_multi_index_type    _index;
         shared_ptr<fork_item>    _head;
   };

} } // xgt::chain
