#include "../helpers.cpp"

using namespace std;
using namespace eosio;

ACTION ascensionwx::setpicture( name devname, 
                                bool ifpicture ) {

  require_auth(get_self());

  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.modify( rewards_itr, get_self(), [&](auto& reward) {
        reward.picture_sent = ifpicture;
  });

}


ACTION ascensionwx::setevmaddr( name miner_or_builder, 
                                string evm_address_str ) {

  // Only self can run this action
  require_auth( get_self() );

  checksum160 evm_address160 = evm_string_to_checksum160( evm_address_str );

  evm_table_t _evmaccounts( name("eosio.evm"), name("eosio.evm").value );
  auto acct_index = _evmaccounts.get_index<"byaddress"_n>();
  
  //uint256_t address = eosio_evm::checksum160ToAddress(evm_address);
  checksum256 address256 = eosio_evm::pad160(evm_address160);
  auto evm_itr = acct_index.find( address256 );

  // If EVM address is not present in the table, call openwallet function to add it
  if ( evm_itr == acct_index.cend() ) {
    action(
      permission_level{ get_self(), "active"_n },
      "eosio.evm"_n , "openwallet"_n,
      std::make_tuple( get_self(), evm_address160)
    ).send();
  }

  // Set the miner's account to hold the EVM address string
  minersv2_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner_or_builder.value );

  if ( miners_itr != _miners.cend() )
    _miners.modify( miners_itr, get_self(), [&](auto& miner) {
      miner.evm_address_str = evm_address_str;
      //miner.evm_address256 = eosio_evm::pad160(evm_address160);
      miner.evm_send_enabled = true;
    });

  // Do same for builder's account
  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( miner_or_builder.value );

  if ( builders_itr != _builders.cend() )
    _builders.modify( builders_itr, get_self(), [&](auto& builder) {
      builder.evm_address = evm_address_str;
      builder.evm_send_enabled = true;
    });
  

}