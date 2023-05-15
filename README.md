# eosio-smart-contract
The native Antelope C++ smart contract files for ground-based Weather Miners

For Kanda Weather (Africa) and AscensionWx (U.S.), and open under MIT license.

Features:
- Temperature/humidity QC quality score based on nearby stations
- Sends mining rewards to weather station owners at "Lock-In" USD rates
- Builder royalties issued for those that 3D-print stations for other users
- Sends rewards as either native or Telos EVM

Dependencies:
- 1.8.0 eosio CDT

Utilizes other on-chain contracts
- delphioracle
- eosio.evm 

Future additions:
- Device-specific digital signatures

Currently deployed as the ascendweathr contrct on Telos Mainnet
https://explorer.telos.net/account/ascendweathr
