#ifdef IS_TEST_NET
#include <boost/test/unit_test.hpp>

#include <xgt/chain/xgt_fwd.hpp>

#include <xgt/protocol/exceptions.hpp>
#include <xgt/protocol/hardfork.hpp>

#include <xgt/chain/block_summary_object.hpp>
#include <xgt/chain/database.hpp>
#include <xgt/chain/history_object.hpp>
#include <xgt/chain/xgt_objects.hpp>

#include <xgt/chain/util/reward.hpp>

#include <xgt/plugins/debug_node/debug_node_plugin.hpp>
#include <xgt/plugins/rc/rc_objects.hpp>
#include <xgt/plugins/rc/resource_count.hpp>

#include <fc/crypto/digest.hpp>

#include "../db_fixture/database_fixture.hpp"

#include <cmath>

using namespace xgt;
using namespace xgt::chain;
using namespace xgt::chain::util;
using namespace xgt::protocol;

BOOST_FIXTURE_TEST_SUITE( operation_time_tests, clean_database_fixture )

BOOST_AUTO_TEST_CASE( comment_payout_equalize )
{
   try
   {
      ACTORS( (alice)(bob)(dave)
              (ulysses)(vivian)(wendy) )

      struct author_actor
      {
         author_actor(
            const std::string& n,
            fc::ecc::private_key pk,
            fc::optional<asset> mpay = fc::optional<asset>() )
            : name(n), private_key(pk), max_accepted_payout(mpay) {}
         std::string             name;
         fc::ecc::private_key    private_key;
         fc::optional< asset >   max_accepted_payout;
      };

      struct voter_actor
      {
         voter_actor( const std::string& n, fc::ecc::private_key pk, std::string fa )
            : name(n), private_key(pk), favorite_author(fa) {}
         std::string             name;
         fc::ecc::private_key    private_key;
         std::string             favorite_author;
      };


      std::vector< author_actor > authors;
      std::vector< voter_actor > voters;

      authors.emplace_back( "alice", alice_private_key );
      authors.emplace_back( "bob"  , bob_private_key, ASSET( "0.000 TBD" ) );
      authors.emplace_back( "dave" , dave_private_key );
      voters.emplace_back( "ulysses", ulysses_private_key, "alice");
      voters.emplace_back( "vivian" , vivian_private_key , "bob"  );
      voters.emplace_back( "wendy"  , wendy_private_key  , "dave" );

      // A,B,D : posters
      // U,V,W : voters

      // set a ridiculously high XGT price ($1 / satoshi) to disable dust threshold
      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "0.001 TESTS" ) ) );

      for( const auto& voter : voters )
      {
         fund( voter.name, 10000 );
         vest( voter.name, 10000 );
      }

      // authors all write in the same block, but Bob declines payout
      for( const auto& author : authors )
      {
         signed_transaction tx;
         comment_operation com;
         com.author = author.name;
         com.permlink = "mypost";
         com.parent_author = XGT_ROOT_POST_PARENT;
         com.parent_permlink = "test";
         com.title = "Hello from "+author.name;
         com.body = "Hello, my name is "+author.name;
         tx.operations.push_back( com );

         if( author.max_accepted_payout.valid() )
         {
            comment_options_operation copt;
            copt.author = com.author;
            copt.permlink = com.permlink;
            copt.max_accepted_payout = *(author.max_accepted_payout);
            tx.operations.push_back( copt );
         }

         tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
         sign( tx, author.private_key );
         db->push_transaction( tx, 0 );
      }

      generate_blocks(1);

      // voters all vote in the same block with the same stake
      for( const auto& voter : voters )
      {
         signed_transaction tx;
         vote_operation vote;
         vote.voter = voter.name;
         vote.author = voter.favorite_author;
         vote.permlink = "mypost";
         vote.weight = XGT_100_PERCENT;
         tx.operations.push_back( vote );
         tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
         sign( tx, voter.private_key );
         db->push_transaction( tx, 0 );
      }

      //auto reward_xgt = db->get_dynamic_global_properties().total_reward_fund_xgt;

      // generate a few blocks to seed the reward fund
      generate_blocks(10);
      //const auto& rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );
      //idump( (rf) );

      generate_blocks( db->get_comment( "alice", string( "mypost" ) ).cashout_time, true );
      /*
      for( const auto& author : authors )
      {
         const account_object& a = db->get_account(author.name);
         ilog( "${n} : ${xgt} ${sbd}", ("n", author.name)("xgt", a.reward_xgt_balance)("sbd", a.reward_sbd_balance) );
      }
      for( const auto& voter : voters )
      {
         const account_object& a = db->get_account(voter.name);
         ilog( "${n} : ${xgt} ${sbd}", ("n", voter.name)("xgt", a.reward_xgt_balance)("sbd", a.reward_sbd_balance) );
      }
      */

      const account_object& alice_account = db->get_account("alice");
      const account_object& bob_account   = db->get_account("bob");
      const account_object& dave_account  = db->get_account("dave");

      BOOST_CHECK( alice_account.reward_sbd_balance == ASSET( "6236.000 TBD" ) );
      BOOST_CHECK( bob_account.reward_sbd_balance == ASSET( "0.000 TBD" ) );
      BOOST_CHECK( dave_account.reward_sbd_balance == alice_account.reward_sbd_balance );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( comment_payout_dust )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing: comment_payout_dust" );

      ACTORS( (alice)(bob) )
      generate_block();

      vest( XGT_INIT_MINER_NAME, "alice", ASSET( "10.000 TESTS" ) );
      vest( XGT_INIT_MINER_NAME, "bob", ASSET( "10.000 TESTS" ) );

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) ) );

      generate_block();
      validate_database();

      comment_operation comment;
      comment.author = "alice";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.title = "test";
      comment.body = "test";

      signed_transaction tx;
      tx.operations.push_back( comment );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );
      validate_database();

      comment.author = "bob";

      tx.clear();
      tx.operations.push_back( comment );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );
      validate_database();

      generate_blocks( db->head_block_time() + db->get_dynamic_global_properties().reverse_auction_seconds );

      vote_operation vote;
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = 81 * XGT_1_PERCENT;

      tx.clear();
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );
      validate_database();

      vote.voter = "bob";
      vote.author = "bob";
      vote.weight = 59 * XGT_1_PERCENT;

      tx.clear();
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );
      validate_database();

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time );

      // If comments are paid out independent of order, then the last satoshi of XGT cannot be divided among them
      const auto rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );
      BOOST_REQUIRE( rf.reward_balance == ASSET( "0.001 TESTS" ) );

      validate_database();

      BOOST_TEST_MESSAGE( "Done" );
   }
   FC_LOG_AND_RETHROW()
}

/*
BOOST_AUTO_TEST_CASE( reward_funds )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing: reward_funds" );

      ACTORS( (alice)(bob) )
      generate_block();

      set_price_feed( price( ASSET( "1.000 TESTS" ), ASSET( "1.000 TBD" ) ) );
      generate_block();

      comment_operation comment;
      vote_operation vote;
      signed_transaction tx;

      comment.author = "alice";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.title = "foo";
      comment.body = "bar";
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( comment );
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( 5 );

      comment.author = "bob";
      comment.parent_author = "alice";
      vote.voter = "bob";
      vote.author = "bob";
      tx.clear();
      tx.operations.push_back( comment );
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time );

      {
         const auto& post_rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );
         const auto& comment_rf = db->get< reward_fund_object, by_name >( XGT_COMMENT_REWARD_FUND_NAME );

         BOOST_REQUIRE( post_rf.reward_balance.amount == 0 );
         BOOST_REQUIRE( comment_rf.reward_balance.amount > 0 );
         BOOST_REQUIRE( db->get_account( "alice" ).reward_sbd_balance.amount > 0 );
         BOOST_REQUIRE( db->get_account( "bob" ).reward_sbd_balance.amount == 0 );
         validate_database();
      }

      generate_blocks( db->get_comment( "bob", string( "test" ) ).cashout_time );

      {
         const auto& post_rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );
         const auto& comment_rf = db->get< reward_fund_object, by_name >( XGT_COMMENT_REWARD_FUND_NAME );

         BOOST_REQUIRE( post_rf.reward_balance.amount > 0 );
         BOOST_REQUIRE( comment_rf.reward_balance.amount == 0 );
         BOOST_REQUIRE( db->get_account( "alice" ).reward_sbd_balance.amount > 0 );
         BOOST_REQUIRE( db->get_account( "bob" ).reward_sbd_balance.amount > 0 );
         validate_database();
      }
   }
   FC_LOG_AND_RETHROW()
}
*/

BOOST_AUTO_TEST_CASE( recent_claims_decay )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing: recent_rshares_2decay" );
      ACTORS( (alice)(bob) )
      generate_block();

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) ) );
      generate_block();

      comment_operation comment;
      vote_operation vote;
      signed_transaction tx;

      comment.author = "alice";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.title = "foo";
      comment.body = "bar";
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( comment );
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      auto alice_vshares = util::evaluate_reward_curve( db->get_comment( "alice", string( "test" ) ).net_rshares.value,
         db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME ).author_reward_curve,
         db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME ).content_constant );

      generate_blocks( 5 );

      comment.author = "bob";
      vote.voter = "bob";
      vote.author = "bob";
      tx.clear();
      tx.operations.push_back( comment );
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time );

      {
         const auto& post_rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );

         BOOST_REQUIRE( post_rf.recent_claims == alice_vshares );
         validate_database();
      }

      auto bob_cashout_time = db->get_comment( "bob", string( "test" ) ).cashout_time;
      auto bob_vshares = util::evaluate_reward_curve( db->get_comment( "bob", string( "test" ) ).net_rshares.value,
         db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME ).author_reward_curve,
         db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME ).content_constant );

      generate_block();

      while( db->head_block_time() < bob_cashout_time )
      {
         alice_vshares -= ( alice_vshares * XGT_BLOCK_INTERVAL ) / XGT_RECENT_RSHARES_DECAY_TIME_HF19.to_seconds();
         const auto& post_rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );

         BOOST_REQUIRE( post_rf.recent_claims == alice_vshares );

         generate_block();

      }

      {
         alice_vshares -= ( alice_vshares * XGT_BLOCK_INTERVAL ) / XGT_RECENT_RSHARES_DECAY_TIME_HF19.to_seconds();
         const auto& post_rf = db->get< reward_fund_object, by_name >( XGT_POST_REWARD_FUND_NAME );

         BOOST_REQUIRE( post_rf.recent_claims == alice_vshares + bob_vshares );
         validate_database();
      }
   }
   FC_LOG_AND_RETHROW()
}

/*
BOOST_AUTO_TEST_CASE( comment_payout )
{
   try
   {
      ACTORS( (alice)(bob)(sam)(dave) )
      fund( "alice", 10000 );
      vest( "alice", 10000 );
      fund( "bob", 7500 );
      vest( "bob", 7500 );
      fund( "sam", 8000 );
      vest( "sam", 8000 );
      fund( "dave", 5000 );
      vest( "dave", 5000 );

      price exchange_rate( ASSET( "1.000 TESTS" ), ASSET( "1.000 TBD" ) );
      set_price_feed( exchange_rate );

      signed_transaction tx;

      BOOST_TEST_MESSAGE( "Creating comments." );

      comment_operation com;
      com.author = "alice";
      com.permlink = "test";
      com.parent_author = "";
      com.parent_permlink = "test";
      com.title = "foo";
      com.body = "bar";
      tx.operations.push_back( com );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      com.author = "bob";
      tx.operations.push_back( com );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "Voting for comments." );

      vote_operation vote;
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      vote.voter = "sam";
      vote.author = "alice";
      tx.operations.push_back( vote );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      vote.voter = "bob";
      vote.author = "bob";
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      vote.voter = "dave";
      vote.author = "bob";
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Generate blocks up until first payout" );

      //generate_blocks( db->get_comment( "bob", string( "test" ) ).cashout_time - XGT_BLOCK_INTERVAL, true );

      auto reward_xgt = db->get_dynamic_global_properties().total_reward_fund_xgt + ASSET( "1.667 TESTS" );
      auto total_rshares2 = db->get_dynamic_global_properties().total_reward_shares2;
      auto bob_comment_rshares = db->get_comment( "bob", string( "test" ) ).net_rshares;
      auto bob_vest_shares = db->get_account( "bob" ).vesting_shares;
      auto bob_sbd_balance = db->get_account( "bob" ).sbd_balance;

      auto bob_comment_payout = asset( ( ( uint128_t( bob_comment_rshares.value ) * bob_comment_rshares.value * reward_xgt.amount.value ) / total_rshares2 ).to_uint64(), XGT_SYMBOL );
      auto bob_comment_discussion_rewards = asset( bob_comment_payout.amount / 4, XGT_SYMBOL );
      bob_comment_payout -= bob_comment_discussion_rewards;
      auto bob_comment_sbd_reward = db->to_sbd( asset( bob_comment_payout.amount / 2, XGT_SYMBOL ) );
      auto bob_comment_vesting_reward = ( bob_comment_payout - asset( bob_comment_payout.amount / 2, XGT_SYMBOL) ) * db->get_dynamic_global_properties().get_vesting_share_price();

      BOOST_TEST_MESSAGE( "Cause first payout" );

      generate_block();

      BOOST_REQUIRE( db->get_dynamic_global_properties().total_reward_fund_xgt == reward_xgt - bob_comment_payout );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).total_payout_value == bob_comment_vesting_reward * db->get_dynamic_global_properties().get_vesting_share_price() + bob_comment_sbd_reward * exchange_rate );
      BOOST_REQUIRE( db->get_account( "bob" ).vesting_shares == bob_vest_shares + bob_comment_vesting_reward );
      BOOST_REQUIRE( db->get_account( "bob" ).sbd_balance == bob_sbd_balance + bob_comment_sbd_reward );

      BOOST_TEST_MESSAGE( "Testing no payout when less than $0.02" );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "alice";
      vote.author = "alice";
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "dave";
      vote.author = "bob";
      vote.weight = XGT_1_PERCENT;
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "bob", string( "test" ) ).cashout_time - XGT_BLOCK_INTERVAL, true );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "bob";
      vote.author = "alice";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "sam";
      tx.operations.push_back( vote );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "dave";
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      bob_vest_shares = db->get_account( "bob" ).vesting_shares;
      bob_sbd_balance = db->get_account( "bob" ).sbd_balance;

      validate_database();

      generate_block();

      BOOST_REQUIRE( bob_vest_shares.amount.value == db->get_account( "bob" ).vesting_shares.amount.value );
      BOOST_REQUIRE( bob_sbd_balance.amount.value == db->get_account( "bob" ).sbd_balance.amount.value );
      validate_database();
   }
   FC_LOG_AND_RETHROW()
}*/

/*
BOOST_AUTO_TEST_CASE( comment_payout )
{
   try
   {
      ACTORS( (alice)(bob)(sam)(dave) )
      fund( "alice", 10000 );
      vest( "alice", 10000 );
      fund( "bob", 7500 );
      vest( "bob", 7500 );
      fund( "sam", 8000 );
      vest( "sam", 8000 );
      fund( "dave", 5000 );
      vest( "dave", 5000 );

      price exchange_rate( ASSET( "1.000 TESTS" ), ASSET( "1.000 TBD" ) );
      set_price_feed( exchange_rate );

      auto gpo = db->get_dynamic_global_properties();

      signed_transaction tx;

      BOOST_TEST_MESSAGE( "Creating comments. " );

      comment_operation com;
      com.author = "alice";
      com.permlink = "test";
      com.parent_permlink = "test";
      com.title = "foo";
      com.body = "bar";
      tx.operations.push_back( com );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      com.author = "bob";
      tx.operations.push_back( com );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "First round of votes." );

      tx.operations.clear();
      tx.signatures.clear();
      vote_operation vote;
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "dave";
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "bob";
      vote.author = "bob";
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "sam";
      tx.operations.push_back( vote );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Generating blocks..." );

      generate_blocks( fc::time_point_sec( db->head_block_time().sec_since_epoch() + XGT_CASHOUT_WINDOW_SECONDS / 2 ), true );

      BOOST_TEST_MESSAGE( "Second round of votes." );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "alice";
      vote.author = "bob";
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      tx.operations.push_back( vote );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "bob";
      vote.author = "alice";
      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "sam";
      tx.operations.push_back( vote );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Generating more blocks..." );

      generate_blocks( db->get_comment( "bob", string( "test" ) ).cashout_time - ( XGT_BLOCK_INTERVAL / 2 ), true );

      BOOST_TEST_MESSAGE( "Check comments have not been paid out." );

      const auto& vote_idx = db->get_index< comment_vote_index >().indices().get< by_comment_voter >();

      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "sam" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "dave" ) ).id  ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "alice" ) ).id ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "bob" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "sam" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value > 0 );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).net_rshares.value > 0 );
      validate_database();

      auto reward_xgt = db->get_dynamic_global_properties().total_reward_fund_xgt + ASSET( "2.000 TESTS" );
      auto total_rshares2 = db->get_dynamic_global_properties().total_reward_shares2;
      auto bob_comment_vote_total = db->get_comment( "bob", string( "test" ) ).total_vote_weight;
      auto bob_comment_rshares = db->get_comment( "bob", string( "test" ) ).net_rshares;
      auto bob_sbd_balance = db->get_account( "bob" ).sbd_balance;
      auto alice_vest_shares = db->get_account( "alice" ).vesting_shares;
      auto bob_vest_shares = db->get_account( "bob" ).vesting_shares;
      auto sam_vest_shares = db->get_account( "sam" ).vesting_shares;
      auto dave_vest_shares = db->get_account( "dave" ).vesting_shares;

      auto bob_comment_payout = asset( ( ( uint128_t( bob_comment_rshares.value ) * bob_comment_rshares.value * reward_xgt.amount.value ) / total_rshares2 ).to_uint64(), XGT_SYMBOL );
      auto bob_comment_vote_rewards = asset( bob_comment_payout.amount / 2, XGT_SYMBOL );
      bob_comment_payout -= bob_comment_vote_rewards;
      auto bob_comment_sbd_reward = asset( bob_comment_payout.amount / 2, XGT_SYMBOL ) * exchange_rate;
      auto bob_comment_vesting_reward = ( bob_comment_payout - asset( bob_comment_payout.amount / 2, XGT_SYMBOL ) ) * db->get_dynamic_global_properties().get_vesting_share_price();
      auto unclaimed_payments = bob_comment_vote_rewards;
      auto alice_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "alice" ) ).id ) )->weight ) * bob_comment_vote_rewards.amount.value ) / bob_comment_vote_total ), XGT_SYMBOL );
      auto alice_vote_vesting = alice_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      auto bob_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "bob" ) ).id ) )->weight ) * bob_comment_vote_rewards.amount.value ) / bob_comment_vote_total ), XGT_SYMBOL );
      auto bob_vote_vesting = bob_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      auto sam_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "sam" ) ).id ) )->weight ) * bob_comment_vote_rewards.amount.value ) / bob_comment_vote_total ), XGT_SYMBOL );
      auto sam_vote_vesting = sam_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      unclaimed_payments -= ( alice_vote_reward + bob_vote_reward + sam_vote_reward );

      BOOST_TEST_MESSAGE( "Generate one block to cause a payout" );

      generate_block();

      auto bob_comment_reward = get_last_operations( 1 )[0].get< comment_reward_operation >();

      BOOST_REQUIRE( db->get_dynamic_global_properties().total_reward_fund_xgt.amount.value == reward_xgt.amount.value - ( bob_comment_payout + bob_comment_vote_rewards - unclaimed_payments ).amount.value );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).total_payout_value.amount.value == ( ( bob_comment_vesting_reward * db->get_dynamic_global_properties().get_vesting_share_price() ) + ( bob_comment_sbd_reward * exchange_rate ) ).amount.value );
      BOOST_REQUIRE( db->get_account( "bob" ).sbd_balance.amount.value == ( bob_sbd_balance + bob_comment_sbd_reward ).amount.value );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value > 0 );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == ( alice_vest_shares + alice_vote_vesting ).amount.value );
      BOOST_REQUIRE( db->get_account( "bob" ).vesting_shares.amount.value == ( bob_vest_shares + bob_vote_vesting + bob_comment_vesting_reward ).amount.value );
      BOOST_REQUIRE( db->get_account( "sam" ).vesting_shares.amount.value == ( sam_vest_shares + sam_vote_vesting ).amount.value );
      BOOST_REQUIRE( db->get_account( "dave" ).vesting_shares.amount.value == dave_vest_shares.amount.value );
      BOOST_REQUIRE( bob_comment_reward.author == "bob" );
      BOOST_REQUIRE( bob_comment_reward.permlink == "test" );
      BOOST_REQUIRE( bob_comment_reward.payout.amount.value == bob_comment_sbd_reward.amount.value );
      BOOST_REQUIRE( bob_comment_reward.vesting_payout.amount.value == bob_comment_vesting_reward.amount.value );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "sam" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "dave" ) ).id  ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "alice" ) ).id ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "bob" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "sam" ) ).id   ) ) == vote_idx.end() );
      validate_database();

      BOOST_TEST_MESSAGE( "Generating blocks up to next comment payout" );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time - ( XGT_BLOCK_INTERVAL / 2 ), true );

      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "sam" ) ).id   ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "dave" ) ).id  ) ) != vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "alice" ) ).id ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "bob" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "sam" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value > 0 );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).net_rshares.value == 0 );
      validate_database();

      BOOST_TEST_MESSAGE( "Generate block to cause payout" );

      reward_xgt = db->get_dynamic_global_properties().total_reward_fund_xgt + ASSET( "2.000 TESTS" );
      total_rshares2 = db->get_dynamic_global_properties().total_reward_shares2;
      auto alice_comment_vote_total = db->get_comment( "alice", string( "test" ) ).total_vote_weight;
      auto alice_comment_rshares = db->get_comment( "alice", string( "test" ) ).net_rshares;
      auto alice_sbd_balance = db->get_account( "alice" ).sbd_balance;
      alice_vest_shares = db->get_account( "alice" ).vesting_shares;
      bob_vest_shares = db->get_account( "bob" ).vesting_shares;
      sam_vest_shares = db->get_account( "sam" ).vesting_shares;
      dave_vest_shares = db->get_account( "dave" ).vesting_shares;

      u256 rs( alice_comment_rshares.value );
      u256 rf( reward_xgt.amount.value );
      u256 trs2 = total_rshares2.hi;
      trs2 = ( trs2 << 64 ) + total_rshares2.lo;
      auto rs2 = rs*rs;

      auto alice_comment_payout = asset( static_cast< uint64_t >( ( rf * rs2 ) / trs2 ), XGT_SYMBOL );
      auto alice_comment_vote_rewards = asset( alice_comment_payout.amount / 2, XGT_SYMBOL );
      alice_comment_payout -= alice_comment_vote_rewards;
      auto alice_comment_sbd_reward = asset( alice_comment_payout.amount / 2, XGT_SYMBOL ) * exchange_rate;
      auto alice_comment_vesting_reward = ( alice_comment_payout - asset( alice_comment_payout.amount / 2, XGT_SYMBOL ) ) * db->get_dynamic_global_properties().get_vesting_share_price();
      unclaimed_payments = alice_comment_vote_rewards;
      alice_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) )->weight ) * alice_comment_vote_rewards.amount.value ) / alice_comment_vote_total ), XGT_SYMBOL );
      alice_vote_vesting = alice_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      bob_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id ) )->weight ) * alice_comment_vote_rewards.amount.value ) / alice_comment_vote_total ), XGT_SYMBOL );
      bob_vote_vesting = bob_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      sam_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "sam" ) ).id ) )->weight ) * alice_comment_vote_rewards.amount.value ) / alice_comment_vote_total ), XGT_SYMBOL );
      sam_vote_vesting = sam_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      auto dave_vote_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "dave" ) ).id ) )->weight ) * alice_comment_vote_rewards.amount.value ) / alice_comment_vote_total ), XGT_SYMBOL );
      auto dave_vote_vesting = dave_vote_reward * db->get_dynamic_global_properties().get_vesting_share_price();
      unclaimed_payments -= ( alice_vote_reward + bob_vote_reward + sam_vote_reward + dave_vote_reward );

      generate_block();
      auto alice_comment_reward = get_last_operations( 1 )[0].get< comment_reward_operation >();

      BOOST_REQUIRE( ( db->get_dynamic_global_properties().total_reward_fund_xgt + alice_comment_payout + alice_comment_vote_rewards - unclaimed_payments ).amount.value == reward_xgt.amount.value );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).total_payout_value.amount.value == ( ( alice_comment_vesting_reward * db->get_dynamic_global_properties().get_vesting_share_price() ) + ( alice_comment_sbd_reward * exchange_rate ) ).amount.value );
      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance.amount.value == ( alice_sbd_balance + alice_comment_sbd_reward ).amount.value );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == ( alice_vest_shares + alice_vote_vesting + alice_comment_vesting_reward ).amount.value );
      BOOST_REQUIRE( db->get_account( "bob" ).vesting_shares.amount.value == ( bob_vest_shares + bob_vote_vesting ).amount.value );
      BOOST_REQUIRE( db->get_account( "sam" ).vesting_shares.amount.value == ( sam_vest_shares + sam_vote_vesting ).amount.value );
      BOOST_REQUIRE( db->get_account( "dave" ).vesting_shares.amount.value == ( dave_vest_shares + dave_vote_vesting ).amount.value );
      BOOST_REQUIRE( alice_comment_reward.author == "alice" );
      BOOST_REQUIRE( alice_comment_reward.permlink == "test" );
      BOOST_REQUIRE( alice_comment_reward.payout.amount.value == alice_comment_sbd_reward.amount.value );
      BOOST_REQUIRE( alice_comment_reward.vesting_payout.amount.value == alice_comment_vesting_reward.amount.value );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "sam" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "dave" ) ).id  ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "alice" ) ).id ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "bob" ) ).id   ) ) == vote_idx.end() );
      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id,   db->get_account( "sam" ) ).id   ) ) == vote_idx.end() );
      validate_database();

      BOOST_TEST_MESSAGE( "Testing no payout when less than $0.02" );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "alice";
      vote.author = "alice";
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "dave";
      vote.author = "bob";
      vote.weight = XGT_1_PERCENT;
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "bob", string( "test" ) ).cashout_time - XGT_BLOCK_INTERVAL, true );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "bob";
      vote.author = "alice";
      vote.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "sam";
      tx.operations.push_back( vote );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote.voter = "dave";
      tx.operations.push_back( vote );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      bob_vest_shares = db->get_account( "bob" ).vesting_shares;
      auto bob_sbd = db->get_account( "bob" ).sbd_balance;

      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "dave" ) ).id ) ) != vote_idx.end() );
      validate_database();

      generate_block();

      BOOST_REQUIRE( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "dave" ) ).id ) ) == vote_idx.end() );
      BOOST_REQUIRE( bob_vest_shares.amount.value == db->get_account( "bob" ).vesting_shares.amount.value );
      BOOST_REQUIRE( bob_sbd.amount.value == db->get_account( "bob" ).sbd_balance.amount.value );
      validate_database();
   }
   FC_LOG_AND_RETHROW()
}

OOST_AUTO_TEST_CASE( nested_comments )
{
   try
   {
      ACTORS( (alice)(bob)(sam)(dave) )
      fund( "alice", 10000 );
      vest( "alice", 10000 );
      fund( "bob", 10000 );
      vest( "bob", 10000 );
      fund( "sam", 10000 );
      vest( "sam", 10000 );
      fund( "dave", 10000 );
      vest( "dave", 10000 );

      price exchange_rate( ASSET( "1.000 TESTS" ), ASSET( "1.000 TBD" ) );
      set_price_feed( exchange_rate );

      signed_transaction tx;
      comment_operation comment_op;
      comment_op.author = "alice";
      comment_op.permlink = "test";
      comment_op.parent_permlink = "test";
      comment_op.title = "foo";
      comment_op.body = "bar";
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      tx.operations.push_back( comment_op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      comment_op.author = "bob";
      comment_op.parent_author = "alice";
      comment_op.parent_permlink = "test";
      tx.operations.clear();
      tx.signatures.clear();
      tx.operations.push_back( comment_op );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      comment_op.author = "sam";
      comment_op.parent_author = "bob";
      tx.operations.clear();
      tx.signatures.clear();
      tx.operations.push_back( comment_op );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      comment_op.author = "dave";
      comment_op.parent_author = "sam";
      tx.operations.clear();
      tx.signatures.clear();
      tx.operations.push_back( comment_op );
      sign( tx, dave_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      vote_operation vote_op;
      vote_op.voter = "alice";
      vote_op.author = "alice";
      vote_op.permlink = "test";
      vote_op.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote_op );
      vote_op.author = "bob";
      tx.operations.push_back( vote_op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote_op.voter = "bob";
      vote_op.author = "alice";
      tx.operations.push_back( vote_op );
      vote_op.author = "bob";
      tx.operations.push_back( vote_op );
      vote_op.author = "dave";
      vote_op.weight = XGT_1_PERCENT * 20;
      tx.operations.push_back( vote_op );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();
      vote_op.voter = "sam";
      vote_op.author = "bob";
      vote_op.weight = XGT_100_PERCENT;
      tx.operations.push_back( vote_op );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time - fc::seconds( XGT_BLOCK_INTERVAL ), true );

      auto gpo = db->get_dynamic_global_properties();
      uint128_t reward_xgt = gpo.total_reward_fund_xgt.amount.value + ASSET( "2.000 TESTS" ).amount.value;
      uint128_t total_rshares2 = gpo.total_reward_shares2;

      auto alice_comment = db->get_comment( "alice", string( "test" ) );
      auto bob_comment = db->get_comment( "bob", string( "test" ) );
      auto sam_comment = db->get_comment( "sam", string( "test" ) );
      auto dave_comment = db->get_comment( "dave", string( "test" ) );

      const auto& vote_idx = db->get_index< comment_vote_index >().indices().get< by_comment_voter >();

      // Calculate total comment rewards and voting rewards.
      auto alice_comment_reward = ( ( reward_xgt * alice_comment.net_rshares.value * alice_comment.net_rshares.value ) / total_rshares2 ).to_uint64();
      total_rshares2 -= uint128_t( alice_comment.net_rshares.value ) * ( alice_comment.net_rshares.value );
      reward_xgt -= alice_comment_reward;
      auto alice_comment_vote_rewards = alice_comment_reward / 2;
      alice_comment_reward -= alice_comment_vote_rewards;

      auto alice_vote_alice_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "alice" ) ).id ) )->weight ) * alice_comment_vote_rewards ) / alice_comment.total_vote_weight ), XGT_SYMBOL );
      auto bob_vote_alice_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "alice", string( "test" ).id, db->get_account( "bob" ) ).id ) )->weight ) * alice_comment_vote_rewards ) / alice_comment.total_vote_weight ), XGT_SYMBOL );
      reward_xgt += alice_comment_vote_rewards - ( alice_vote_alice_reward + bob_vote_alice_reward ).amount.value;

      auto bob_comment_reward = ( ( reward_xgt * bob_comment.net_rshares.value * bob_comment.net_rshares.value ) / total_rshares2 ).to_uint64();
      total_rshares2 -= uint128_t( bob_comment.net_rshares.value ) * bob_comment.net_rshares.value;
      reward_xgt -= bob_comment_reward;
      auto bob_comment_vote_rewards = bob_comment_reward / 2;
      bob_comment_reward -= bob_comment_vote_rewards;

      auto alice_vote_bob_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "alice" ) ).id ) )->weight ) * bob_comment_vote_rewards ) / bob_comment.total_vote_weight ), XGT_SYMBOL );
      auto bob_vote_bob_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "bob" ) ).id ) )->weight ) * bob_comment_vote_rewards ) / bob_comment.total_vote_weight ), XGT_SYMBOL );
      auto sam_vote_bob_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "bob", string( "test" ).id, db->get_account( "sam" ) ).id ) )->weight ) * bob_comment_vote_rewards ) / bob_comment.total_vote_weight ), XGT_SYMBOL );
      reward_xgt += bob_comment_vote_rewards - ( alice_vote_bob_reward + bob_vote_bob_reward + sam_vote_bob_reward ).amount.value;

      auto dave_comment_reward = ( ( reward_xgt * dave_comment.net_rshares.value * dave_comment.net_rshares.value ) / total_rshares2 ).to_uint64();
      total_rshares2 -= uint128_t( dave_comment.net_rshares.value ) * dave_comment.net_rshares.value;
      reward_xgt -= dave_comment_reward;
      auto dave_comment_vote_rewards = dave_comment_reward / 2;
      dave_comment_reward -= dave_comment_vote_rewards;

      auto bob_vote_dave_reward = asset( static_cast< uint64_t >( ( u256( vote_idx.find( boost::make_tuple( db->get_comment( "dave", string( "test" ).id, db->get_account( "bob" ) ).id ) )->weight ) * dave_comment_vote_rewards ) / dave_comment.total_vote_weight ), XGT_SYMBOL );
      reward_xgt += dave_comment_vote_rewards - bob_vote_dave_reward.amount.value;

      // Calculate rewards paid to parent posts
      auto alice_pays_alice_sbd = alice_comment_reward / 2;
      auto alice_pays_alice_vest = alice_comment_reward - alice_pays_alice_sbd;
      auto bob_pays_bob_sbd = bob_comment_reward / 2;
      auto bob_pays_bob_vest = bob_comment_reward - bob_pays_bob_sbd;
      auto dave_pays_dave_sbd = dave_comment_reward / 2;
      auto dave_pays_dave_vest = dave_comment_reward - dave_pays_dave_sbd;

      auto bob_pays_alice_sbd = bob_pays_bob_sbd / 2;
      auto bob_pays_alice_vest = bob_pays_bob_vest / 2;
      bob_pays_bob_sbd -= bob_pays_alice_sbd;
      bob_pays_bob_vest -= bob_pays_alice_vest;

      auto dave_pays_sam_sbd = dave_pays_dave_sbd / 2;
      auto dave_pays_sam_vest = dave_pays_dave_vest / 2;
      dave_pays_dave_sbd -= dave_pays_sam_sbd;
      dave_pays_dave_vest -= dave_pays_sam_vest;
      auto dave_pays_bob_sbd = dave_pays_sam_sbd / 2;
      auto dave_pays_bob_vest = dave_pays_sam_vest / 2;
      dave_pays_sam_sbd -= dave_pays_bob_sbd;
      dave_pays_sam_vest -= dave_pays_bob_vest;
      auto dave_pays_alice_sbd = dave_pays_bob_sbd / 2;
      auto dave_pays_alice_vest = dave_pays_bob_vest / 2;
      dave_pays_bob_sbd -= dave_pays_alice_sbd;
      dave_pays_bob_vest -= dave_pays_alice_vest;

      // Calculate total comment payouts
      auto alice_comment_total_payout = db->to_sbd( asset( alice_pays_alice_sbd + alice_pays_alice_vest, XGT_SYMBOL ) );
      alice_comment_total_payout += db->to_sbd( asset( bob_pays_alice_sbd + bob_pays_alice_vest, XGT_SYMBOL ) );
      alice_comment_total_payout += db->to_sbd( asset( dave_pays_alice_sbd + dave_pays_alice_vest, XGT_SYMBOL ) );
      auto bob_comment_total_payout = db->to_sbd( asset( bob_pays_bob_sbd + bob_pays_bob_vest, XGT_SYMBOL ) );
      bob_comment_total_payout += db->to_sbd( asset( dave_pays_bob_sbd + dave_pays_bob_vest, XGT_SYMBOL ) );
      auto sam_comment_total_payout = db->to_sbd( asset( dave_pays_sam_sbd + dave_pays_sam_vest, XGT_SYMBOL ) );
      auto dave_comment_total_payout = db->to_sbd( asset( dave_pays_dave_sbd + dave_pays_dave_vest, XGT_SYMBOL ) );

      auto alice_starting_vesting = db->get_account( "alice" ).vesting_shares;
      auto alice_starting_sbd = db->get_account( "alice" ).sbd_balance;
      auto bob_starting_vesting = db->get_account( "bob" ).vesting_shares;
      auto bob_starting_sbd = db->get_account( "bob" ).sbd_balance;
      auto sam_starting_vesting = db->get_account( "sam" ).vesting_shares;
      auto sam_starting_sbd = db->get_account( "sam" ).sbd_balance;
      auto dave_starting_vesting = db->get_account( "dave" ).vesting_shares;
      auto dave_starting_sbd = db->get_account( "dave" ).sbd_balance;

      generate_block();

      gpo = db->get_dynamic_global_properties();

      // Calculate vesting share rewards from voting.
      auto alice_vote_alice_vesting = alice_vote_alice_reward * gpo.get_vesting_share_price();
      auto bob_vote_alice_vesting = bob_vote_alice_reward * gpo.get_vesting_share_price();
      auto alice_vote_bob_vesting = alice_vote_bob_reward * gpo.get_vesting_share_price();
      auto bob_vote_bob_vesting = bob_vote_bob_reward * gpo.get_vesting_share_price();
      auto sam_vote_bob_vesting = sam_vote_bob_reward * gpo.get_vesting_share_price();
      auto bob_vote_dave_vesting = bob_vote_dave_reward * gpo.get_vesting_share_price();

      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).total_payout_value.amount.value == alice_comment_total_payout.amount.value );
      BOOST_REQUIRE( db->get_comment( "bob", string( "test" ) ).total_payout_value.amount.value == bob_comment_total_payout.amount.value );
      BOOST_REQUIRE( db->get_comment( "sam", string( "test" ) ).total_payout_value.amount.value == sam_comment_total_payout.amount.value );
      BOOST_REQUIRE( db->get_comment( "dave", string( "test" ) ).total_payout_value.amount.value == dave_comment_total_payout.amount.value );

      // ops 0-3, 5-6, and 10 are comment reward ops
      auto ops = get_last_operations( 13 );

      BOOST_TEST_MESSAGE( "Checking Virtual Operation Correctness" );

      curate_reward_operation cur_vop;
      comment_reward_operation com_vop = ops[0].get< comment_reward_operation >();

      BOOST_REQUIRE( com_vop.author == "alice" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "dave" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == dave_pays_alice_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == dave_pays_alice_vest );

      com_vop = ops[1].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "bob" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "dave" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == dave_pays_bob_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == dave_pays_bob_vest );

      com_vop = ops[2].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "sam" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "dave" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == dave_pays_sam_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == dave_pays_sam_vest );

      com_vop = ops[3].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "dave" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "dave" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == dave_pays_dave_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == dave_pays_dave_vest );

      cur_vop = ops[4].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "bob" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == bob_vote_dave_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "dave" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      com_vop = ops[5].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "alice" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "bob" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == bob_pays_alice_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == bob_pays_alice_vest );

      com_vop = ops[6].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "bob" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "bob" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == bob_pays_bob_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == bob_pays_bob_vest );

      cur_vop = ops[7].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "sam" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == sam_vote_bob_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "bob" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      cur_vop = ops[8].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "bob" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == bob_vote_bob_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "bob" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      cur_vop = ops[9].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "alice" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == alice_vote_bob_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "bob" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      com_vop = ops[10].get< comment_reward_operation >();
      BOOST_REQUIRE( com_vop.author == "alice" );
      BOOST_REQUIRE( com_vop.permlink == "test" );
      BOOST_REQUIRE( com_vop.originating_author == "alice" );
      BOOST_REQUIRE( com_vop.originating_permlink == "test" );
      BOOST_REQUIRE( com_vop.payout.amount.value == alice_pays_alice_sbd );
      BOOST_REQUIRE( ( com_vop.vesting_payout * gpo.get_vesting_share_price() ).amount.value == alice_pays_alice_vest );

      cur_vop = ops[11].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "bob" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == bob_vote_alice_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "alice" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      cur_vop = ops[12].get< curate_reward_operation >();
      BOOST_REQUIRE( cur_vop.curator == "alice" );
      BOOST_REQUIRE( cur_vop.reward.amount.value == alice_vote_alice_vesting.amount.value );
      BOOST_REQUIRE( cur_vop.comment_author == "alice" );
      BOOST_REQUIRE( cur_vop.comment_permlink == "test" );

      BOOST_TEST_MESSAGE( "Checking account balances" );

      auto alice_total_sbd = alice_starting_sbd + asset( alice_pays_alice_sbd + bob_pays_alice_sbd + dave_pays_alice_sbd, XGT_SYMBOL ) * exchange_rate;
      auto alice_total_vesting = alice_starting_vesting + asset( alice_pays_alice_vest + bob_pays_alice_vest + dave_pays_alice_vest + alice_vote_alice_reward.amount + alice_vote_bob_reward.amount, XGT_SYMBOL ) * gpo.get_vesting_share_price();
      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance.amount.value == alice_total_sbd.amount.value );
      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == alice_total_vesting.amount.value );

      auto bob_total_sbd = bob_starting_sbd + asset( bob_pays_bob_sbd + dave_pays_bob_sbd, XGT_SYMBOL ) * exchange_rate;
      auto bob_total_vesting = bob_starting_vesting + asset( bob_pays_bob_vest + dave_pays_bob_vest + bob_vote_alice_reward.amount + bob_vote_bob_reward.amount + bob_vote_dave_reward.amount, XGT_SYMBOL ) * gpo.get_vesting_share_price();
      BOOST_REQUIRE( db->get_account( "bob" ).sbd_balance.amount.value == bob_total_sbd.amount.value );
      BOOST_REQUIRE( db->get_account( "bob" ).vesting_shares.amount.value == bob_total_vesting.amount.value );

      auto sam_total_sbd = sam_starting_sbd + asset( dave_pays_sam_sbd, XGT_SYMBOL ) * exchange_rate;
      auto sam_total_vesting = bob_starting_vesting + asset( dave_pays_sam_vest + sam_vote_bob_reward.amount, XGT_SYMBOL ) * gpo.get_vesting_share_price();
      BOOST_REQUIRE( db->get_account( "sam" ).sbd_balance.amount.value == sam_total_sbd.amount.value );
      BOOST_REQUIRE( db->get_account( "sam" ).vesting_shares.amount.value == sam_total_vesting.amount.value );

      auto dave_total_sbd = dave_starting_sbd + asset( dave_pays_dave_sbd, XGT_SYMBOL ) * exchange_rate;
      auto dave_total_vesting = dave_starting_vesting + asset( dave_pays_dave_vest, XGT_SYMBOL ) * gpo.get_vesting_share_price();
      BOOST_REQUIRE( db->get_account( "dave" ).sbd_balance.amount.value == dave_total_sbd.amount.value );
      BOOST_REQUIRE( db->get_account( "dave" ).vesting_shares.amount.value == dave_total_vesting.amount.value );
   }
   FC_LOG_AND_RETHROW()
}
*/


BOOST_AUTO_TEST_CASE( vesting_withdrawals )
{
   try
   {
      ACTORS( (alice) )
      fund( "alice", 100000 );
      vest( "alice", 100000 );

      const auto& new_alice = db->get_account( "alice" );

      BOOST_TEST_MESSAGE( "Setting up withdrawal" );

      signed_transaction tx;
      withdraw_vesting_operation op;
      op.account = "alice";
      op.vesting_shares = asset( new_alice.vesting_shares.amount / 2, VESTS_SYMBOL );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      auto next_withdrawal = db->head_block_time() + XGT_VESTING_WITHDRAW_INTERVAL_SECONDS;
      asset vesting_shares = new_alice.vesting_shares;
      asset original_vesting = vesting_shares;
      asset withdraw_rate = new_alice.vesting_withdraw_rate;

      BOOST_TEST_MESSAGE( "Generating block up to first withdrawal" );
      generate_blocks( next_withdrawal - ( XGT_BLOCK_INTERVAL / 2 ), true);

      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == vesting_shares.amount.value );

      BOOST_TEST_MESSAGE( "Generating block to cause withdrawal" );
      generate_block();

      auto fill_op = get_last_operations( 1 )[0].get< fill_vesting_withdraw_operation >();
      auto gpo = db->get_dynamic_global_properties();

      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == ( vesting_shares - withdraw_rate ).amount.value );
      BOOST_REQUIRE( ( withdraw_rate * gpo.get_vesting_share_price() ).amount.value - db->get_account( "alice" ).balance.amount.value <= 1 ); // Check a range due to differences in the share price
      BOOST_REQUIRE( fill_op.from_account == "alice" );
      BOOST_REQUIRE( fill_op.to_account == "alice" );
      BOOST_REQUIRE( fill_op.withdrawn.amount.value == withdraw_rate.amount.value );
      BOOST_REQUIRE( std::abs( ( fill_op.deposited - fill_op.withdrawn * gpo.get_vesting_share_price() ).amount.value ) <= 1 );
      validate_database();

      BOOST_TEST_MESSAGE( "Generating the rest of the blocks in the withdrawal" );

      vesting_shares = db->get_account( "alice" ).vesting_shares;
      auto balance = db->get_account( "alice" ).balance;
      auto old_next_vesting = db->get_account( "alice" ).next_vesting_withdrawal;

      for( int i = 1; i < XGT_VESTING_WITHDRAW_INTERVALS - 1; i++ )
      {
         generate_blocks( db->head_block_time() + XGT_VESTING_WITHDRAW_INTERVAL_SECONDS );

         const auto& alice = db->get_account( "alice" );

         gpo = db->get_dynamic_global_properties();
         fill_op = get_last_operations( 2 )[1].get< fill_vesting_withdraw_operation >();

         BOOST_REQUIRE( alice.vesting_shares.amount.value == ( vesting_shares - withdraw_rate ).amount.value );
         BOOST_REQUIRE( balance.amount.value + ( withdraw_rate * gpo.get_vesting_share_price() ).amount.value - alice.balance.amount.value <= 1 );
         BOOST_REQUIRE( fill_op.from_account == "alice" );
         BOOST_REQUIRE( fill_op.to_account == "alice" );
         BOOST_REQUIRE( fill_op.withdrawn.amount.value == withdraw_rate.amount.value );
         BOOST_REQUIRE( std::abs( ( fill_op.deposited - fill_op.withdrawn * gpo.get_vesting_share_price() ).amount.value ) <= 1 );

         if ( i == XGT_VESTING_WITHDRAW_INTERVALS - 1 )
            BOOST_REQUIRE( alice.next_vesting_withdrawal == fc::time_point_sec::maximum() );
         else
            BOOST_REQUIRE( alice.next_vesting_withdrawal.sec_since_epoch() == ( old_next_vesting + XGT_VESTING_WITHDRAW_INTERVAL_SECONDS ).sec_since_epoch() );

         validate_database();

         vesting_shares = alice.vesting_shares;
         balance = alice.balance;
         old_next_vesting = alice.next_vesting_withdrawal;
      }

      BOOST_TEST_MESSAGE( "Generating one more block to finish vesting withdrawal" );
      generate_blocks( db->head_block_time() + XGT_VESTING_WITHDRAW_INTERVAL_SECONDS, true );

      BOOST_REQUIRE( db->get_account( "alice" ).next_vesting_withdrawal.sec_since_epoch() == fc::time_point_sec::maximum().sec_since_epoch() );
      BOOST_REQUIRE( db->get_account( "alice" ).vesting_shares.amount.value == ( original_vesting - op.vesting_shares ).amount.value );

      validate_database();
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( vesting_withdraw_route )
{
   try
   {
      ACTORS( (alice)(bob)(sam) )

      auto original_vesting = alice.vesting_shares;

      fund( "alice", 1040000 );
      vest( "alice", 1040000 );

      auto withdraw_amount = alice.vesting_shares - original_vesting;

      BOOST_TEST_MESSAGE( "Setup vesting withdraw" );
      withdraw_vesting_operation wv;
      wv.account = "alice";
      wv.vesting_shares = withdraw_amount;

      signed_transaction tx;
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      tx.operations.push_back( wv );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "Setting up bob destination" );
      set_withdraw_vesting_route_operation op;
      op.from_account = "alice";
      op.to_account = "bob";
      op.percent = XGT_1_PERCENT * 50;
      op.auto_vest = true;
      tx.operations.push_back( op );

      BOOST_TEST_MESSAGE( "Setting up sam destination" );
      op.to_account = "sam";
      op.percent = XGT_1_PERCENT * 30;
      op.auto_vest = false;
      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Setting up first withdraw" );

      auto vesting_withdraw_rate = alice.vesting_withdraw_rate;
      auto old_alice_balance = alice.balance;
      auto old_alice_vesting = alice.vesting_shares;
      auto old_bob_balance = bob.balance;
      auto old_bob_vesting = bob.vesting_shares;
      auto old_sam_balance = sam.balance;
      auto old_sam_vesting = sam.vesting_shares;
      generate_blocks( alice.next_vesting_withdrawal, true );

      {
         const auto& alice = db->get_account( "alice" );
         const auto& bob = db->get_account( "bob" );
         const auto& sam = db->get_account( "sam" );

         BOOST_REQUIRE( alice.vesting_shares == old_alice_vesting - vesting_withdraw_rate );
         BOOST_REQUIRE( alice.balance == old_alice_balance + asset( ( vesting_withdraw_rate.amount * XGT_1_PERCENT * 20 ) / XGT_100_PERCENT, VESTS_SYMBOL ) * db->get_dynamic_global_properties().get_vesting_share_price() );
         BOOST_REQUIRE( bob.vesting_shares == old_bob_vesting + asset( ( vesting_withdraw_rate.amount * XGT_1_PERCENT * 50 ) / XGT_100_PERCENT, VESTS_SYMBOL ) );
         BOOST_REQUIRE( bob.balance == old_bob_balance );
         BOOST_REQUIRE( sam.vesting_shares == old_sam_vesting );
         BOOST_REQUIRE( sam.balance ==  old_sam_balance + asset( ( vesting_withdraw_rate.amount * XGT_1_PERCENT * 30 ) / XGT_100_PERCENT, VESTS_SYMBOL ) * db->get_dynamic_global_properties().get_vesting_share_price() );

         old_alice_balance = alice.balance;
         old_alice_vesting = alice.vesting_shares;
         old_bob_balance = bob.balance;
         old_bob_vesting = bob.vesting_shares;
         old_sam_balance = sam.balance;
         old_sam_vesting = sam.vesting_shares;
      }

      BOOST_TEST_MESSAGE( "Test failure with greater than 100% destination assignment" );

      tx.operations.clear();
      tx.signatures.clear();

      op.to_account = "sam";
      op.percent = XGT_1_PERCENT * 50 + 1;
      tx.operations.push_back( op );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      XGT_REQUIRE_THROW( db->push_transaction( tx, 0 ), fc::exception );

      BOOST_TEST_MESSAGE( "Test from_account receiving no withdraw" );

      tx.operations.clear();
      tx.signatures.clear();

      op.to_account = "sam";
      op.percent = XGT_1_PERCENT * 50;
      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_account( "alice" ).next_vesting_withdrawal, true );
      {
         const auto& alice = db->get_account( "alice" );
         const auto& bob = db->get_account( "bob" );
         const auto& sam = db->get_account( "sam" );

         BOOST_REQUIRE( alice.vesting_shares == old_alice_vesting - vesting_withdraw_rate );
         BOOST_REQUIRE( alice.balance == old_alice_balance );
         BOOST_REQUIRE( bob.vesting_shares == old_bob_vesting + asset( ( vesting_withdraw_rate.amount * XGT_1_PERCENT * 50 ) / XGT_100_PERCENT, VESTS_SYMBOL ) );
         BOOST_REQUIRE( bob.balance == old_bob_balance );
         BOOST_REQUIRE( sam.vesting_shares == old_sam_vesting );
         BOOST_REQUIRE( sam.balance ==  old_sam_balance + asset( ( vesting_withdraw_rate.amount * XGT_1_PERCENT * 50 ) / XGT_100_PERCENT, VESTS_SYMBOL ) * db->get_dynamic_global_properties().get_vesting_share_price() );
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( feed_publish_mean )
{
   try
   {
      resize_shared_mem( 1024 * 1024 * 32 );

      ACTORS( (alice0)(alice1)(alice2)(alice3)(alice4)(alice5)(alice6) )

      BOOST_TEST_MESSAGE( "Setup" );

      generate_blocks( 30 / XGT_BLOCK_INTERVAL );

      vector< string > accounts;
      accounts.push_back( "alice0" );
      accounts.push_back( "alice1" );
      accounts.push_back( "alice2" );
      accounts.push_back( "alice3" );
      accounts.push_back( "alice4" );
      accounts.push_back( "alice5" );
      accounts.push_back( "alice6" );

      vector< private_key_type > keys;
      keys.push_back( alice0_private_key );
      keys.push_back( alice1_private_key );
      keys.push_back( alice2_private_key );
      keys.push_back( alice3_private_key );
      keys.push_back( alice4_private_key );
      keys.push_back( alice5_private_key );
      keys.push_back( alice6_private_key );

      vector< feed_publish_operation > ops;
      vector< signed_transaction > txs;

      // Upgrade accounts to witnesses
      for( int i = 0; i < 7; i++ )
      {
         transfer( XGT_INIT_MINER_NAME, accounts[i], asset( 10000, XGT_SYMBOL ) );
         witness_create( accounts[i], keys[i], "foo.bar", keys[i].get_public_key(), 1000 );

         ops.push_back( feed_publish_operation() );
         ops[i].publisher = accounts[i];

         txs.push_back( signed_transaction() );
      }

      ops[0].exchange_rate = price( asset( 1000, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL ) );
      ops[1].exchange_rate = price( asset( 942, SBD_SYMBOL ),  asset( 100000, XGT_SYMBOL ) );
      ops[2].exchange_rate = price( asset( 1020, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL ) );
      ops[3].exchange_rate = price( asset( 1031, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL ) );
      ops[4].exchange_rate = price( asset( 1010, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL ) );
      ops[5].exchange_rate = price( asset( 1026, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL ) );
      ops[6].exchange_rate = price( asset( 980, SBD_SYMBOL ),  asset( 100000, XGT_SYMBOL ) );

      for( int i = 0; i < 7; i++ )
      {
         txs[i].set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
         txs[i].operations.push_back( ops[i] );
         sign( txs[i], keys[i] );
         db->push_transaction( txs[i], 0 );
      }

      BOOST_TEST_MESSAGE( "Jump forward an hour" );

      generate_blocks( XGT_BLOCKS_PER_HOUR ); // Jump forward 1 hour
      BOOST_TEST_MESSAGE( "Get feed history object" );
      feed_history_object feed_history = db->get_feed_history();
      BOOST_TEST_MESSAGE( "Check state" );
      BOOST_REQUIRE( feed_history.current_median_history == price( asset( 1010, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL) ) );
      BOOST_REQUIRE( feed_history.price_history[ 0 ] == price( asset( 1010, SBD_SYMBOL ), asset( 100000, XGT_SYMBOL) ) );
      validate_database();

      for ( int i = 0; i < 23; i++ )
      {
         BOOST_TEST_MESSAGE( "Updating ops" );

         for( int j = 0; j < 7; j++ )
         {
            txs[j].operations.clear();
            txs[j].signatures.clear();
            ops[j].exchange_rate = price( asset( ops[j].exchange_rate.base.amount + 1, SBD_SYMBOL ), ops[j].exchange_rate.quote );
            txs[j].set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
            txs[j].operations.push_back( ops[j] );
            sign( txs[j], keys[j] );
            db->push_transaction( txs[j], 0 );
         }

         BOOST_TEST_MESSAGE( "Generate Blocks" );

         generate_blocks( XGT_BLOCKS_PER_HOUR ); // Jump forward 1 hour

         BOOST_TEST_MESSAGE( "Check feed_history" );

         feed_history = db->get(feed_history_id_type());
         BOOST_REQUIRE( feed_history.current_median_history == feed_history.price_history[ ( i + 1 ) / 2 + ( i % 2 ? 0 : 1 ) ] );
         BOOST_REQUIRE( feed_history.price_history[ i + 1 ] == ops[4].exchange_rate );
         validate_database();
      }
   }
   FC_LOG_AND_RETHROW();
}

BOOST_AUTO_TEST_CASE( convert_delay )
{
   try
   {
      ACTORS( (alice) )
      generate_block();
      vest( XGT_INIT_MINER_NAME, "alice", ASSET( "10.000 TESTS" ) );
      fund( "alice", ASSET( "25.000 TBD" ) );

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.250 TESTS" ) ) );

      convert_operation op;
      signed_transaction tx;

      auto start_balance = ASSET( "25.000 TBD" );

      BOOST_TEST_MESSAGE( "Setup conversion to TESTS" );
      tx.operations.clear();
      tx.signatures.clear();
      op.owner = "alice";
      op.amount = asset( 2000, SBD_SYMBOL );
      op.requestid = 2;
      tx.operations.push_back( op );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Generating Blocks up to conversion block" );
      generate_blocks( db->head_block_time() + XGT_CONVERSION_DELAY - fc::seconds( XGT_BLOCK_INTERVAL / 2 ), true );

      BOOST_TEST_MESSAGE( "Verify conversion is not applied" );
      const auto& alice_2 = db->get_account( "alice" );
      const auto& convert_request_idx = db->get_index< convert_request_index >().indices().get< by_owner >();
      auto convert_request = convert_request_idx.find( boost::make_tuple( "alice", 2 ) );

      BOOST_REQUIRE( convert_request != convert_request_idx.end() );
      BOOST_REQUIRE( alice_2.balance.amount.value == 0 );
      BOOST_REQUIRE( alice_2.sbd_balance.amount.value == ( start_balance - op.amount ).amount.value );
      validate_database();

      BOOST_TEST_MESSAGE( "Generate one more block" );
      generate_block();

      BOOST_TEST_MESSAGE( "Verify conversion applied" );
      const auto& alice_3 = db->get_account( "alice" );
      auto vop = get_last_operations( 1 )[0].get< fill_convert_request_operation >();

      convert_request = convert_request_idx.find( boost::make_tuple( "alice", 2 ) );
      BOOST_REQUIRE( convert_request == convert_request_idx.end() );
      BOOST_REQUIRE( alice_3.balance.amount.value == 2500 );
      BOOST_REQUIRE( alice_3.sbd_balance.amount.value == ( start_balance - op.amount ).amount.value );
      BOOST_REQUIRE( vop.owner == "alice" );
      BOOST_REQUIRE( vop.requestid == 2 );
      BOOST_REQUIRE( vop.amount_in.amount.value == ASSET( "2.000 TBD" ).amount.value );
      BOOST_REQUIRE( vop.amount_out.amount.value == ASSET( "2.500 TESTS" ).amount.value );
      validate_database();
   }
   FC_LOG_AND_RETHROW();
}

BOOST_AUTO_TEST_CASE( xgt_inflation )
{
   try
   {
   /*
      BOOST_TEST_MESSAGE( "Testing XGT Inflation until the vesting start block" );

      auto gpo = db->get_dynamic_global_properties();
      auto virtual_supply = gpo.virtual_supply;
      auto witness_name = db->get_scheduled_witness(1);
      auto old_witness_balance = db->get_account( witness_name ).balance;
      auto old_witness_shares = db->get_account( witness_name ).vesting_shares;

      auto new_rewards = std::max( XGT_MIN_CONTENT_REWARD, asset( ( XGT_CONTENT_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) )
         + std::max( XGT_MIN_CURATE_REWARD, asset( ( XGT_CURATE_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
      auto witness_pay = std::max( XGT_MIN_PRODUCER_REWARD, asset( ( XGT_PRODUCER_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
      auto witness_pay_shares = asset( 0, VESTS_SYMBOL );
      auto new_vesting_xgt = asset( 0, XGT_SYMBOL );
      auto new_vesting_shares = gpo.total_vesting_shares;

      if ( db->get_account( witness_name ).vesting_shares.amount.value == 0 )
      {
         new_vesting_xgt += witness_pay;
         new_vesting_shares += witness_pay * ( gpo.total_vesting_shares / gpo.total_vesting_fund_xgt );
      }

      auto new_supply = gpo.current_supply + new_rewards + witness_pay + new_vesting_xgt;
      new_rewards += gpo.total_reward_fund_xgt;
      new_vesting_xgt += gpo.total_vesting_fund_xgt;

      generate_block();

      gpo = db->get_dynamic_global_properties();

      BOOST_REQUIRE( gpo.current_supply.amount.value == new_supply.amount.value );
      BOOST_REQUIRE( gpo.virtual_supply.amount.value == new_supply.amount.value );
      BOOST_REQUIRE( gpo.total_reward_fund_xgt.amount.value == new_rewards.amount.value );
      BOOST_REQUIRE( gpo.total_vesting_fund_xgt.amount.value == new_vesting_xgt.amount.value );
      BOOST_REQUIRE( gpo.total_vesting_shares.amount.value == new_vesting_shares.amount.value );
      BOOST_REQUIRE( db->get_account( witness_name ).balance.amount.value == ( old_witness_balance + witness_pay ).amount.value );

      validate_database();

      while( db->head_block_num() < XGT_START_VESTING_BLOCK - 1)
      {
         virtual_supply = gpo.virtual_supply;
         witness_name = db->get_scheduled_witness(1);
         old_witness_balance = db->get_account( witness_name ).balance;
         old_witness_shares = db->get_account( witness_name ).vesting_shares;


         new_rewards = std::max( XGT_MIN_CONTENT_REWARD, asset( ( XGT_CONTENT_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) )
            + std::max( XGT_MIN_CURATE_REWARD, asset( ( XGT_CURATE_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         witness_pay = std::max( XGT_MIN_PRODUCER_REWARD, asset( ( XGT_PRODUCER_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         new_vesting_xgt = asset( 0, XGT_SYMBOL );
         new_vesting_shares = gpo.total_vesting_shares;

         if ( db->get_account( witness_name ).vesting_shares.amount.value == 0 )
         {
            new_vesting_xgt += witness_pay;
            witness_pay_shares = witness_pay * gpo.get_vesting_share_price();
            new_vesting_shares += witness_pay_shares;
            new_supply += witness_pay;
            witness_pay = asset( 0, XGT_SYMBOL );
         }

         new_supply = gpo.current_supply + new_rewards + witness_pay + new_vesting_xgt;
         new_rewards += gpo.total_reward_fund_xgt;
         new_vesting_xgt += gpo.total_vesting_fund_xgt;

         generate_block();

         gpo = db->get_dynamic_global_properties();

         BOOST_REQUIRE( gpo.current_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.virtual_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.total_reward_fund_xgt.amount.value == new_rewards.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_fund_xgt.amount.value == new_vesting_xgt.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_shares.amount.value == new_vesting_shares.amount.value );
         BOOST_REQUIRE( db->get_account( witness_name ).balance.amount.value == ( old_witness_balance + witness_pay ).amount.value );
         BOOST_REQUIRE( db->get_account( witness_name ).vesting_shares.amount.value == ( old_witness_shares + witness_pay_shares ).amount.value );

         validate_database();
      }

      BOOST_TEST_MESSAGE( "Testing up to the start block for miner voting" );

      while( db->head_block_num() < XGT_START_MINER_VOTING_BLOCK - 1 )
      {
         virtual_supply = gpo.virtual_supply;
         witness_name = db->get_scheduled_witness(1);
         old_witness_balance = db->get_account( witness_name ).balance;

         new_rewards = std::max( XGT_MIN_CONTENT_REWARD, asset( ( XGT_CONTENT_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) )
            + std::max( XGT_MIN_CURATE_REWARD, asset( ( XGT_CURATE_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         witness_pay = std::max( XGT_MIN_PRODUCER_REWARD, asset( ( XGT_PRODUCER_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         auto witness_pay_shares = asset( 0, VESTS_SYMBOL );
         new_vesting_xgt = asset( ( witness_pay + new_rewards ).amount * 9, XGT_SYMBOL );
         new_vesting_shares = gpo.total_vesting_shares;

         if ( db->get_account( witness_name ).vesting_shares.amount.value == 0 )
         {
            new_vesting_xgt += witness_pay;
            witness_pay_shares = witness_pay * gpo.get_vesting_share_price();
            new_vesting_shares += witness_pay_shares;
            new_supply += witness_pay;
            witness_pay = asset( 0, XGT_SYMBOL );
         }

         new_supply = gpo.current_supply + new_rewards + witness_pay + new_vesting_xgt;
         new_rewards += gpo.total_reward_fund_xgt;
         new_vesting_xgt += gpo.total_vesting_fund_xgt;

         generate_block();

         gpo = db->get_dynamic_global_properties();

         BOOST_REQUIRE( gpo.current_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.virtual_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.total_reward_fund_xgt.amount.value == new_rewards.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_fund_xgt.amount.value == new_vesting_xgt.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_shares.amount.value == new_vesting_shares.amount.value );
         BOOST_REQUIRE( db->get_account( witness_name ).balance.amount.value == ( old_witness_balance + witness_pay ).amount.value );
         BOOST_REQUIRE( db->get_account( witness_name ).vesting_shares.amount.value == ( old_witness_shares + witness_pay_shares ).amount.value );

         validate_database();
      }

      for( int i = 0; i < XGT_BLOCKS_PER_DAY; i++ )
      {
         virtual_supply = gpo.virtual_supply;
         witness_name = db->get_scheduled_witness(1);
         old_witness_balance = db->get_account( witness_name ).balance;

         new_rewards = std::max( XGT_MIN_CONTENT_REWARD, asset( ( XGT_CONTENT_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) )
            + std::max( XGT_MIN_CURATE_REWARD, asset( ( XGT_CURATE_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         witness_pay = std::max( XGT_MIN_PRODUCER_REWARD, asset( ( XGT_PRODUCER_APR * gpo.virtual_supply.amount ) / ( XGT_BLOCKS_PER_YEAR * 100 ), XGT_SYMBOL ) );
         witness_pay_shares = witness_pay * gpo.get_vesting_share_price();
         new_vesting_xgt = asset( ( witness_pay + new_rewards ).amount * 9, XGT_SYMBOL ) + witness_pay;
         new_vesting_shares = gpo.total_vesting_shares + witness_pay_shares;
         new_supply = gpo.current_supply + new_rewards + new_vesting_xgt;
         new_rewards += gpo.total_reward_fund_xgt;
         new_vesting_xgt += gpo.total_vesting_fund_xgt;

         generate_block();

         gpo = db->get_dynamic_global_properties();

         BOOST_REQUIRE( gpo.current_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.virtual_supply.amount.value == new_supply.amount.value );
         BOOST_REQUIRE( gpo.total_reward_fund_xgt.amount.value == new_rewards.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_fund_xgt.amount.value == new_vesting_xgt.amount.value );
         BOOST_REQUIRE( gpo.total_vesting_shares.amount.value == new_vesting_shares.amount.value );
         BOOST_REQUIRE( db->get_account( witness_name ).vesting_shares.amount.value == ( old_witness_shares + witness_pay_shares ).amount.value );

         validate_database();
      }

      virtual_supply = gpo.virtual_supply;
      vesting_shares = gpo.total_vesting_shares;
      vesting_xgt = gpo.total_vesting_fund_xgt;
      reward_xgt = gpo.total_reward_fund_xgt;

      witness_name = db->get_scheduled_witness(1);
      old_witness_shares = db->get_account( witness_name ).vesting_shares;

      generate_block();

      gpo = db->get_dynamic_global_properties();

      BOOST_REQUIRE_EQUAL( gpo.total_vesting_fund_xgt.amount.value,
         ( vesting_xgt.amount.value
            + ( ( ( uint128_t( virtual_supply.amount.value ) / 10 ) / XGT_BLOCKS_PER_YEAR ) * 9 )
            + ( uint128_t( virtual_supply.amount.value ) / 100 / XGT_BLOCKS_PER_YEAR ) ).to_uint64() );
      BOOST_REQUIRE_EQUAL( gpo.total_reward_fund_xgt.amount.value,
         reward_xgt.amount.value + virtual_supply.amount.value / 10 / XGT_BLOCKS_PER_YEAR + virtual_supply.amount.value / 10 / XGT_BLOCKS_PER_DAY );
      BOOST_REQUIRE_EQUAL( db->get_account( witness_name ).vesting_shares.amount.value,
         old_witness_shares.amount.value + ( asset( ( ( virtual_supply.amount.value / XGT_BLOCKS_PER_YEAR ) * XGT_1_PERCENT ) / XGT_100_PERCENT, XGT_SYMBOL ) * ( vesting_shares / vesting_xgt ) ).amount.value );
      validate_database();
      */
   }
   FC_LOG_AND_RETHROW();
}

BOOST_AUTO_TEST_CASE( sbd_interest )
{
   try
   {
      ACTORS( (alice)(bob) )
      generate_block();
      vest( XGT_INIT_MINER_NAME, "alice", ASSET( "10.000 TESTS" ) );
      vest( XGT_INIT_MINER_NAME, "bob", ASSET( "10.000 TESTS" ) );

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) ) );

      BOOST_TEST_MESSAGE( "Testing interest over smallest interest period" );

      convert_operation op;
      signed_transaction tx;

      fund( "alice", ASSET( "31.903 TBD" ) );

      auto start_time = db->get_account( "alice" ).sbd_seconds_last_update;
      auto alice_sbd = db->get_account( "alice" ).sbd_balance;

      generate_blocks( db->head_block_time() + fc::seconds( XGT_SBD_INTEREST_COMPOUND_INTERVAL_SEC ), true );

      transfer_operation transfer;
      transfer.to = "bob";
      transfer.from = "alice";
      transfer.amount = ASSET( "1.000 TBD" );
      tx.operations.clear();
      tx.signatures.clear();
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      tx.operations.push_back( transfer );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      auto gpo = db->get_dynamic_global_properties();
      auto interest_op = get_last_operations( 1 )[0].get< interest_operation >();

      BOOST_REQUIRE( gpo.sbd_interest_rate > 0 );
      BOOST_REQUIRE( static_cast<uint64_t>(db->get_account( "alice" ).sbd_balance.amount.value) == alice_sbd.amount.value - ASSET( "1.000 TBD" ).amount.value + ( ( ( ( uint128_t( alice_sbd.amount.value ) * ( db->head_block_time() - start_time ).to_seconds() ) / XGT_SECONDS_PER_YEAR ) * gpo.sbd_interest_rate ) / XGT_100_PERCENT ).to_uint64() );
      BOOST_REQUIRE( interest_op.owner == "alice" );
      BOOST_REQUIRE( interest_op.interest.amount.value == db->get_account( "alice" ).sbd_balance.amount.value - ( alice_sbd.amount.value - ASSET( "1.000 TBD" ).amount.value ) );
      validate_database();

      BOOST_TEST_MESSAGE( "Testing interest under interest period" );

      start_time = db->get_account( "alice" ).sbd_seconds_last_update;
      alice_sbd = db->get_account( "alice" ).sbd_balance;

      generate_blocks( db->head_block_time() + fc::seconds( XGT_SBD_INTEREST_COMPOUND_INTERVAL_SEC / 2 ), true );

      tx.operations.clear();
      tx.signatures.clear();
      tx.operations.push_back( transfer );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance.amount.value == alice_sbd.amount.value - ASSET( "1.000 TBD" ).amount.value );
      validate_database();

      auto alice_coindays = uint128_t( alice_sbd.amount.value ) * ( db->head_block_time() - start_time ).to_seconds();
      alice_sbd = db->get_account( "alice" ).sbd_balance;
      start_time = db->get_account( "alice" ).sbd_seconds_last_update;

      BOOST_TEST_MESSAGE( "Testing longer interest period" );

      generate_blocks( db->head_block_time() + fc::seconds( ( XGT_SBD_INTEREST_COMPOUND_INTERVAL_SEC * 7 ) / 3 ), true );

      tx.operations.clear();
      tx.signatures.clear();
      tx.operations.push_back( transfer );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( static_cast<uint64_t>(db->get_account( "alice" ).sbd_balance.amount.value) == alice_sbd.amount.value - ASSET( "1.000 TBD" ).amount.value + ( ( ( ( uint128_t( alice_sbd.amount.value ) * ( db->head_block_time() - start_time ).to_seconds() + alice_coindays ) / XGT_SECONDS_PER_YEAR ) * gpo.sbd_interest_rate ) / XGT_100_PERCENT ).to_uint64() );
      validate_database();
   }
   FC_LOG_AND_RETHROW();
}

BOOST_AUTO_TEST_CASE( post_rate_limit )
{
   try
   {
      ACTORS( (alice) )

      fund( "alice", 10000 );
      vest( "alice", 10000 );

      comment_operation op;
      op.author = "alice";
      op.permlink = "test1";
      op.parent_author = "";
      op.parent_permlink = "test";
      op.body = "test";

      signed_transaction tx;

      tx.operations.push_back( op );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test1" ) ).reward_weight == XGT_100_PERCENT );

      tx.operations.clear();
      tx.signatures.clear();

      generate_blocks( db->head_block_time() + XGT_MIN_ROOT_COMMENT_INTERVAL + fc::seconds( XGT_BLOCK_INTERVAL ), true );

      op.permlink = "test2";

      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test2" ) ).reward_weight == XGT_100_PERCENT );

      generate_blocks( db->head_block_time() + XGT_MIN_ROOT_COMMENT_INTERVAL + fc::seconds( XGT_BLOCK_INTERVAL ), true );

      tx.operations.clear();
      tx.signatures.clear();

      op.permlink = "test3";

      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test3" ) ).reward_weight == XGT_100_PERCENT );

      generate_blocks( db->head_block_time() + XGT_MIN_ROOT_COMMENT_INTERVAL + fc::seconds( XGT_BLOCK_INTERVAL ), true );

      tx.operations.clear();
      tx.signatures.clear();

      op.permlink = "test4";

      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test4" ) ).reward_weight == XGT_100_PERCENT );

      generate_blocks( db->head_block_time() + XGT_MIN_ROOT_COMMENT_INTERVAL + fc::seconds( XGT_BLOCK_INTERVAL ), true );

      tx.operations.clear();
      tx.signatures.clear();

      op.permlink = "test5";

      tx.operations.push_back( op );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test5" ) ).reward_weight == XGT_100_PERCENT );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( comment_freeze )
{
   try
   {
      ACTORS( (alice)(bob)(sam)(dave) )
      fund( "alice", 10000 );
      fund( "bob", 10000 );
      fund( "sam", 10000 );
      fund( "dave", 10000 );

      vest( "alice", 10000 );
      vest( "bob", 10000 );
      vest( "sam", 10000 );
      vest( "dave", 10000 );

      auto exchange_rate = price( ASSET( "1.000 TBD" ), ASSET( "1.250 TESTS" ) );
      set_price_feed( exchange_rate );

      signed_transaction tx;

      comment_operation comment;
      comment.author = "alice";
      comment.parent_author = "";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.body = "test";

      tx.operations.push_back( comment );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      comment.body = "test2";

      generate_block();

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( comment );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      vote_operation vote;
      vote.weight = XGT_100_PERCENT;
      vote.voter = "bob";
      vote.author = "alice";
      vote.permlink = "test";

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).last_payout == fc::time_point_sec::min() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time != fc::time_point_sec::min() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time != fc::time_point_sec::maximum() );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time, true );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).last_payout == db->head_block_time() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time == fc::time_point_sec::maximum() );

      vote.voter = "sam";

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, sam_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time == fc::time_point_sec::maximum() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).abs_rshares.value == 0 );

      vote.voter = "bob";
      vote.weight = XGT_100_PERCENT * -1;

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time == fc::time_point_sec::maximum() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).abs_rshares.value == 0 );

      vote.voter = "dave";
      vote.weight = 0;

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, dave_private_key );

      db->push_transaction( tx, 0 );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).cashout_time == fc::time_point_sec::maximum() );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).net_rshares.value == 0 );
      BOOST_REQUIRE( db->get_comment( "alice", string( "test" ) ).abs_rshares.value == 0 );

      comment.body = "test4";

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( comment );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 ); // Works now in #1714
   }
   FC_LOG_AND_RETHROW()
}

// This test is too intensive without optimizations. Disable it when we build in debug
BOOST_AUTO_TEST_CASE( sbd_stability )
{
   #ifndef DEBUG
   try
   {
      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
         {
            gpo.sps_fund_percent = 0;
            gpo.content_reward_percent = 75 * XGT_1_PERCENT;
         });
      }, database::skip_witness_signature );

      resize_shared_mem( 1024 * 1024 * 512 ); // Due to number of blocks in the test, it requires a large file. (64 MB)

      auto debug_key = "5JdouSvkK75TKWrJixYufQgePT21V7BAVWbNUWt3ktqhPmy8Z78"; //get_dev_key debug node

      ACTORS( (alice)(bob)(sam)(dave)(greg) );

      fund( "alice", 10000 );
      fund( "bob", 10000 );

      vest( "alice", 10000 );
      vest( "bob", 10000 );

      auto exchange_rate = price( ASSET( "1.000 TBD" ), ASSET( "10.000 TESTS" ) );
      set_price_feed( exchange_rate );

      BOOST_REQUIRE( db->get_dynamic_global_properties().sbd_print_rate == XGT_100_PERCENT );

      comment_operation comment;
      comment.author = "alice";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.title = "test";
      comment.body = "test";

      signed_transaction tx;
      tx.operations.push_back( comment );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      vote_operation vote;
      vote.voter = "bob";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;

      tx.operations.clear();
      tx.signatures.clear();

      tx.operations.push_back( vote );
      sign( tx, bob_private_key );
      db->push_transaction( tx, 0 );

      BOOST_TEST_MESSAGE( "Generating blocks up to comment payout" );

      db_plugin->debug_generate_blocks_until( debug_key, fc::time_point_sec( db->get_comment( comment.author, comment.permlink ).cashout_time.sec_since_epoch() - 2 * XGT_BLOCK_INTERVAL ), true, database::skip_witness_signature );

      auto& gpo = db->get_dynamic_global_properties();

      BOOST_TEST_MESSAGE( "Changing sam and gpo to set up market cap conditions" );

      asset sbd_balance = asset( ( gpo.virtual_supply.amount * ( gpo.sbd_stop_percent + 112 ) ) / XGT_100_PERCENT, XGT_SYMBOL ) * exchange_rate;
      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_account( "sam" ), [&]( account_object& a )
         {
            a.sbd_balance = sbd_balance;
         });
      }, database::skip_witness_signature );

      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
         {
            gpo.current_sbd_supply = sbd_balance + db.get_account( XGT_TREASURY_ACCOUNT ).sbd_balance;
            gpo.virtual_supply = gpo.virtual_supply + sbd_balance * exchange_rate;
         });
      }, database::skip_witness_signature );

      validate_database();

      db_plugin->debug_generate_blocks( debug_key, 1, database::skip_witness_signature );

      auto comment_reward = ( gpo.total_reward_fund_xgt.amount + 2000 ) - ( ( gpo.total_reward_fund_xgt.amount + 2000 ) * 25 * XGT_1_PERCENT ) / XGT_100_PERCENT ;
      comment_reward /= 2;
      auto sbd_reward = ( comment_reward * gpo.sbd_print_rate ) / XGT_100_PERCENT;
      auto alice_sbd = db->get_account( "alice" ).sbd_balance + db->get_account( "alice" ).reward_sbd_balance + asset( sbd_reward, XGT_SYMBOL ) * exchange_rate;
      auto alice_xgt = db->get_account( "alice" ).balance + db->get_account( "alice" ).reward_xgt_balance ;

      BOOST_TEST_MESSAGE( "Checking printing SBD has slowed" );
      BOOST_REQUIRE( db->get_dynamic_global_properties().sbd_print_rate < XGT_100_PERCENT );

      BOOST_TEST_MESSAGE( "Pay out comment and check rewards are paid as XGT" );
      db_plugin->debug_generate_blocks( debug_key, 1, database::skip_witness_signature );

      validate_database();

      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance + db->get_account( "alice" ).reward_sbd_balance == alice_sbd );
      BOOST_REQUIRE( db->get_account( "alice" ).balance + db->get_account( "alice" ).reward_xgt_balance > alice_xgt );

      BOOST_TEST_MESSAGE( "Letting percent market cap fall to sbd_start_percent to verify printing of SBD turns back on" );

      // Get close to sbd_start_percent for printing SBD to start again, but not all the way
      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_account( "sam" ), [&]( account_object& a )
         {
            a.sbd_balance = asset( ( ( gpo.sbd_start_percent - 9 ) * sbd_balance.amount ) / gpo.sbd_stop_percent, SBD_SYMBOL );
         });
      }, database::skip_witness_signature );

      auto current_sbd_supply = alice_sbd + asset( ( ( gpo.sbd_start_percent - 9 ) * sbd_balance.amount ) / gpo.sbd_stop_percent, SBD_SYMBOL ) + db->get_account( XGT_TREASURY_ACCOUNT ).sbd_balance;

      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
         {
            gpo.current_sbd_supply = current_sbd_supply;
         });
      }, database::skip_witness_signature );

      db_plugin->debug_generate_blocks( debug_key, 1, database::skip_witness_signature );
      validate_database();

      BOOST_REQUIRE( db->get_dynamic_global_properties().sbd_print_rate < XGT_100_PERCENT );

      auto last_print_rate = db->get_dynamic_global_properties().sbd_print_rate;

      // Keep producing blocks until printing SBD is back
      while( ( db->get_dynamic_global_properties().current_sbd_supply * exchange_rate ).amount >= ( db->get_dynamic_global_properties().virtual_supply.amount * db->get_dynamic_global_properties().sbd_start_percent ) / XGT_100_PERCENT )
      {
         auto& gpo = db->get_dynamic_global_properties();
         BOOST_REQUIRE( gpo.sbd_print_rate >= last_print_rate );
         last_print_rate = gpo.sbd_print_rate;
         db_plugin->debug_generate_blocks( debug_key, 1, database::skip_witness_signature );
         if( db->head_block_num() % 1000 == 0 )
            validate_database();
      }

      validate_database();

      BOOST_REQUIRE( db->get_dynamic_global_properties().sbd_print_rate == XGT_100_PERCENT );
   }
   FC_LOG_AND_RETHROW()
   #endif
}

BOOST_AUTO_TEST_CASE( sbd_price_feed_limit )
{
   try
   {
      ACTORS( (alice) );
      generate_block();
      vest( XGT_INIT_MINER_NAME, "alice", ASSET( "10.000 TESTS" ) );

      price exchange_rate( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) );
      set_price_feed( exchange_rate );

      comment_operation comment;
      comment.author = "alice";
      comment.permlink = "test";
      comment.parent_permlink = "test";
      comment.title = "test";
      comment.body = "test";

      vote_operation vote;
      vote.voter = "alice";
      vote.author = "alice";
      vote.permlink = "test";
      vote.weight = XGT_100_PERCENT;

      signed_transaction tx;
      tx.operations.push_back( comment );
      tx.operations.push_back( vote );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );

      generate_blocks( db->get_comment( "alice", string( "test" ) ).cashout_time, true );

      BOOST_TEST_MESSAGE( "Setting SBD percent to greater than 10% market cap." );

      db->skip_price_feed_limit_check = false;
      const auto& gpo = db->get_dynamic_global_properties();
      auto new_exchange_rate = price( gpo.current_sbd_supply, asset( ( XGT_100_PERCENT ) * gpo.current_supply.amount, XGT_SYMBOL ) );
      set_price_feed( new_exchange_rate );
      set_price_feed( new_exchange_rate );

      BOOST_REQUIRE( db->get_feed_history().current_median_history > new_exchange_rate && db->get_feed_history().current_median_history < exchange_rate );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( clear_null_account )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing clearing the null account's balances on block" );

      ACTORS( (alice) );
      generate_block();

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) ) );

      fund( "alice", ASSET( "10.000 TESTS" ) );
      fund( "alice", ASSET( "10.000 TBD" ) );

      transfer_operation transfer1;
      transfer1.from = "alice";
      transfer1.to = XGT_NULL_ACCOUNT;
      transfer1.amount = ASSET( "1.000 TESTS" );

      transfer_operation transfer2;
      transfer2.from = "alice";
      transfer2.to = XGT_NULL_ACCOUNT;
      transfer2.amount = ASSET( "2.000 TBD" );

      transfer_to_vesting_operation vest;
      vest.from = "alice";
      vest.to = XGT_NULL_ACCOUNT;
      vest.amount = ASSET( "3.000 TESTS" );

      transfer_to_savings_operation save1;
      save1.from = "alice";
      save1.to = XGT_NULL_ACCOUNT;
      save1.amount = ASSET( "4.000 TESTS" );

      transfer_to_savings_operation save2;
      save2.from = "alice";
      save2.to = XGT_NULL_ACCOUNT;
      save2.amount = ASSET( "5.000 TBD" );

      BOOST_TEST_MESSAGE( "--- Transferring to NULL Account" );

      signed_transaction tx;
      tx.operations.push_back( transfer1 );
      tx.operations.push_back( transfer2 );
      tx.operations.push_back( vest );
      tx.operations.push_back( save1);
      tx.operations.push_back( save2 );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );
      db->push_transaction( tx, 0 );
      validate_database();

      db_plugin->debug_update( [=]( database& db )
      {
         db.modify( db.get_account( XGT_NULL_ACCOUNT ), [&]( account_object& a )
         {
            a.reward_xgt_balance = ASSET( "1.000 TESTS" );
            a.reward_sbd_balance = ASSET( "1.000 TBD" );
            a.reward_vesting_balance = ASSET( "1.000000 VESTS" );
            a.reward_vesting_xgt = ASSET( "1.000 TESTS" );
         });

         db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
         {
            gpo.current_supply += ASSET( "2.000 TESTS" );
            gpo.virtual_supply += ASSET( "3.000 TESTS" );
            gpo.current_sbd_supply += ASSET( "1.000 TBD" );
            gpo.pending_rewarded_vesting_shares += ASSET( "1.000000 VESTS" );
            gpo.pending_rewarded_vesting_xgt += ASSET( "1.000 TESTS" );
         });
      });

      validate_database();

      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).balance == ASSET( "1.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).sbd_balance == ASSET( "2.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).vesting_shares > ASSET( "0.000000 VESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).savings_balance == ASSET( "4.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).savings_sbd_balance == ASSET( "5.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_sbd_balance == ASSET( "1.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_xgt_balance == ASSET( "1.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_vesting_balance == ASSET( "1.000000 VESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_vesting_xgt == ASSET( "1.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( "alice" ).balance == ASSET( "2.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance == ASSET( "3.000 TBD" ) );

      BOOST_TEST_MESSAGE( "--- Generating block to clear balances" );
      generate_block();
      validate_database();

      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).balance == ASSET( "0.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).sbd_balance == ASSET( "0.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).vesting_shares == ASSET( "0.000000 VESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).savings_balance == ASSET( "0.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).savings_sbd_balance == ASSET( "0.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_sbd_balance == ASSET( "0.000 TBD" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_xgt_balance == ASSET( "0.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_vesting_balance == ASSET( "0.000000 VESTS" ) );
      BOOST_REQUIRE( db->get_account( XGT_NULL_ACCOUNT ).reward_vesting_xgt == ASSET( "0.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( "alice" ).balance == ASSET( "2.000 TESTS" ) );
      BOOST_REQUIRE( db->get_account( "alice" ).sbd_balance == ASSET( "3.000 TBD" ) );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( generate_account_subsidies )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing: generate_account_subsidies" );

      // The purpose of this test is to validate the dynamics of available_account_subsidies.

      // set_subsidy_budget creates a lot of blocks, so there should be enough for a few accounts
      // half-life of 10 minutes
      flat_map< string, vector<char> > props;
      props["account_subsidy_budget"] = fc::raw::pack_to_vector( int32_t( 5123 ) );
      props["account_subsidy_decay"] = fc::raw::pack_to_vector( uint32_t( 249617279 ) );
      set_witness_props( props );

      while( true )
      {
         const witness_schedule_object& wso = db->get_witness_schedule_object();
         if(    (wso.account_subsidy_rd.budget_per_time_unit == 5123)
             && (wso.account_subsidy_rd.decay_params.decay_per_time_unit == 249617279) )
            break;
         generate_block();
      }

      auto is_pool_in_equilibrium = []( int64_t pool, int32_t budget, const rd_decay_params& decay_params ) -> bool
      {
         int64_t decay = rd_compute_pool_decay( decay_params, pool, 1 );
         return (decay == budget);
      };

      const witness_schedule_object& wso = db->get_witness_schedule_object();
      BOOST_CHECK_EQUAL( wso.account_subsidy_rd.resource_unit, XGT_ACCOUNT_SUBSIDY_PRECISION );
      BOOST_CHECK_EQUAL( wso.account_subsidy_rd.budget_per_time_unit, 5123 );
      BOOST_CHECK(  is_pool_in_equilibrium( int64_t( wso.account_subsidy_rd.pool_eq )  , wso.account_subsidy_rd.budget_per_time_unit, wso.account_subsidy_rd.decay_params ) );
      BOOST_CHECK( !is_pool_in_equilibrium( int64_t( wso.account_subsidy_rd.pool_eq )-1, wso.account_subsidy_rd.budget_per_time_unit, wso.account_subsidy_rd.decay_params ) );

      int64_t pool = db->get_dynamic_global_properties().available_account_subsidies;

      while( true )
      {
         const dynamic_global_property_object& gpo = db->get_dynamic_global_properties();
         BOOST_CHECK_EQUAL( pool, gpo.available_account_subsidies );
         if( gpo.available_account_subsidies >= 100 * XGT_ACCOUNT_SUBSIDY_PRECISION )
            break;
         generate_block();
         pool = pool + 5123 - ((249617279 * pool) >> XGT_RD_DECAY_DENOM_SHIFT);
      }

      validate_database();
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( account_subsidy_witness_limits )
{
   try
   {
      BOOST_TEST_MESSAGE( "Testing: account_subsidy_witness_limits" );

      ACTORS( (alice) )
      generate_block();

      set_price_feed( price( ASSET( "1.000 TBD" ), ASSET( "1.000 TESTS" ) ) );

      claim_account_operation op;
      signed_transaction tx;

      BOOST_TEST_MESSAGE( "--- Test rejecting claim account when block is generated by a timeshare witness" );

      // set_subsidy_budget creates a lot of blocks, so there should be enough for a few accounts
      // half-life of 10 minutes
      flat_map< string, vector<char> > props;
      props["account_subsidy_budget"] = fc::raw::pack_to_vector( int32_t( 5000 ) );
      props["account_subsidy_decay"] = fc::raw::pack_to_vector( uint32_t( 249617279 ) );
      set_witness_props( props );

      while( true )
      {
         const dynamic_global_property_object& gpo = db->get_dynamic_global_properties();
         if( gpo.available_account_subsidies >= 100 * XGT_ACCOUNT_SUBSIDY_PRECISION )
            break;
         generate_block();
      }

      // Verify the timeshare witness can't create subsidized accounts
      while( db->get< witness_object, by_name >( db->get_scheduled_witness( 1 ) ).schedule != witness_object::timeshare )
      {
         generate_block();
      }

      op.creator = "alice";
      op.fee = ASSET( "0.000 TESTS" );
      tx.operations.push_back( op );
      tx.set_expiration( db->head_block_time() + XGT_MAX_TIME_UNTIL_EXPIRATION );
      sign( tx, alice_private_key );

      BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == 0 );
      BOOST_CHECK( db->_pending_tx.size() == 0 );

      // Pushes successfully
      db->push_transaction( tx, 0 );
      BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == 1 );
      BOOST_CHECK( db->_pending_tx.size() == 1 );

      do
      {
         generate_block();

         // The transaction fails in generate_block(), meaning it is removed from the local node's transaction list
         BOOST_CHECK_EQUAL( db->fetch_block_by_number( db->head_block_num() )->transactions.size(), 0 );
         BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == 0 );
         BOOST_CHECK_EQUAL( db->_pending_tx.size(), 0 );
      } while( db->get< witness_object, by_name >( db->get_scheduled_witness( 1 ) ).schedule == witness_object::timeshare );

      db->push_transaction( tx, 0 );
      BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == 1 );
      BOOST_CHECK( db->_pending_tx.size() == 1 );
      // But generate another block, as a non-time-share witness, and it works
      generate_block();
      BOOST_CHECK_EQUAL( db->fetch_block_by_number( db->head_block_num() )->transactions.size(), 1 );
      BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == 1 );
      BOOST_CHECK_EQUAL( db->_pending_tx.size(), 0 );

      while( db->get< witness_object, by_name >( db->get_scheduled_witness( 1 ) ).schedule == witness_object::timeshare )
      {
         generate_block();
      }

      fc::time_point_sec expiration = db->head_block_time() + fc::seconds(60);
      size_t n = size_t( db->get< witness_object, by_name >( db->get_scheduled_witness( 1 ) ).available_witness_account_subsidies / XGT_ACCOUNT_SUBSIDY_PRECISION );

      ilog( "Creating ${np1} transactions", ("np1", n+1) );
      // Create n+1 transactions
      for( size_t i=0; i<=n; i++ )
      {
         tx.signatures.clear();
         tx.set_expiration( expiration );
         sign( tx, alice_private_key );
         db->push_transaction( tx, 0 );
         expiration += fc::seconds(3);
      }
      BOOST_CHECK( db->get_account( "alice" ).pending_claimed_accounts == n+2 );
      BOOST_CHECK_EQUAL( db->_pending_tx.size(), n+1 );
      generate_block();
      BOOST_CHECK_EQUAL( db->fetch_block_by_number( db->head_block_num() )->transactions.size(), n );
      BOOST_CHECK_EQUAL( db->_pending_tx.size(), 1 );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
#endif
