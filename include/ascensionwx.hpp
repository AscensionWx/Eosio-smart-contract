#include <eosio/eosio.hpp>
#include <math.h> // pow() and trig functions

using namespace std;
using namespace eosio;

CONTRACT ascensionwx : public contract {
  public:
    using contract::contract;

    // Should be called by device

    ACTION updatefirm( string latest_firmware_version );
    
    ACTION newperiod( uint64_t period_start_time ); // also updates tlos_usd rate

    ACTION addsensor( name devname,
                      float latitude_city,
                      float longitude_city );

    ACTION addminer( name devname,
                     name miner); // also looks up if in EVM

    ACTION addevmminer( name devname, 
                        checksum160 evm_address );

    ACTION addbuilder( name devname,
                       name builder,
                       string enclosure_type);

    ACTION submitdata(name devname,
                      float pressure_hpa,
                      float temperature_c, 
                      float humidity_percent,
                      uint8_t flags);

    ACTION submitgps( name devname,
                      float latitude_deg,
                      float longitude_deg,                      
                      float elev_gps_m,
                      float lat_deg_geo,
                      float lon_deg_geo);

    ACTION setpicture( name devname, bool ifpicture );

    ACTION setmultiply( name devname_or_miner, 
                        float multiplier );

    ACTION setbuildmult( name builder,
                         float multiplier );

    ACTION setevmaddr( name miner_or_builder,
                       checksum160 evm_address );

    ACTION chngminerpay(  name token_contract,
                          float miner_amt_per_great_obs,
                          float miner_amt_per_good_obs,
                          float miner_amt_per_poor_obs );
    
    ACTION manualpayall( int num_hours, string memo );

    ACTION addtoken( name token_contract,
                      string symbol_letters,
                      bool usd_denominated,
                      uint8_t precision);

    ACTION addflag( uint64_t bit_value,
                    string processing_step,
                    string issue,
                    string explanation );
    
    ACTION rewardfactor( float factor );

    ACTION removesensor( name devname ); // deletes from reward and weather table too
    ACTION removeminer(name miner);
    ACTION removereward(name devname);
    ACTION removeobs(name devname);
    //ACTION removetoken( name token_contract );

  private:

    // Local functions (not actions)
    void sendReward( name miner, name devname );

    void updateMinerBalance( name miner, uint8_t quality_score, float rewards_multiplier );
    void updateBuilderBalance( name builder, float rewards_multiplier );
    void payoutMiner( name miner, string memo );
    void payoutBuilder( name builder );

    string evmLookup( name miner );
    void handleIfSensorAlreadyHere( name devname, float lat, float lon );

    float calcDistance( float lat1, float lon1, float lat2, float lon2 ); // Calculate distance between two points
    float degToRadians( float degrees );
    float calcDewPoint( float temperature, float humidity );

    bool check_bit( uint8_t device_flags, uint8_t target_flag );
    bool if_indoor_flagged( uint8_t flags, float temperature_c );
    bool if_physical_damage( uint8_t flags );

    void handleClimateContracts(name devname, float latitude_deg, float longitude_deg);
    void set_flags();
    
    /* This function encodes the noaa station id into a unique
        uint64 so that it can be inserted into a data table
    */
    static uint64_t noaa_to_uint64(string id) { 
      // Convert to lower case
      for_each(id.begin(), id.end(), [](char & c){c = tolower(c);});

      // replace all numbers with a new letter to preserve uniqueness
      replace( id.begin(), id.end(), '0', 'a');
      replace( id.begin(), id.end(), '1', 'b');
      replace( id.begin(), id.end(), '2', 'c');
      replace( id.begin(), id.end(), '3', 'd');
      replace( id.begin(), id.end(), '4', 'e');
      replace( id.begin(), id.end(), '5', 'f');
      replace( id.begin(), id.end(), '6', 'g');
      replace( id.begin(), id.end(), '7', 'h');
      replace( id.begin(), id.end(), '8', 'i');
      replace( id.begin(), id.end(), '9', 'j');

      return name(id).value;
    }

    TABLE parametersv1 {

        uint64_t period_start_time;
        uint64_t period_end_time;
        string latest_firmware_version;

        // Ony one row
        auto primary_key() const { return 0; }
    };
    typedef multi_index<name("parametersv1"), parametersv1> parameters_table_t;

    // Data Table List
    TABLE sensorsv1 {
        name devname;
        uint64_t devname_uint64;
        uint64_t time_created;
        string message_type;
        string firmware_version;
        string enclosure_type;

        uint64_t last_gps_lock;
        uint64_t next_sunrise_time;
        uint64_t next_sunset_time;
        float day_anomaly_avg;
        float day_anomaly_num_samples;
        float night_anomaly_avg;
        float night_anomaly_num_samples;

        bool low_voltage;
        bool high_voltage;
        bool permanently_damaged;
        bool in_transit;
        bool one_hotspot;
        bool active_this_period;
        bool active_last_period;
        bool has_helium_miner;
        bool allow_new_memo;

        auto  primary_key() const { return devname.value; }
    };
    typedef multi_index<name("sensorsv1"), sensorsv1> sensors_table_t;

    TABLE weather {
      name devname;
      uint64_t unix_time_s;
      double latitude_deg;
      double longitude_deg;
      uint16_t elevation_gps_m;
      float pressure_hpa;
      float temperature_c;
      float humidity_percent;
      float dew_point;
      uint8_t flags;
      string loc_accuracy;

      auto primary_key() const { return devname.value; }
      uint64_t by_unixtime() const { return unix_time_s; }
      double by_latitude() const { return latitude_deg; }
      double by_longitude() const { return longitude_deg; }
      // TODO: custom index based on having / not having specific flag
    };
    //using observations_index = multi_index<"observations"_n, observations>;
    typedef multi_index<name("weather"), 
                        weather,
                        indexed_by<"unixtime"_n, const_mem_fun<weather, uint64_t, &weather::by_unixtime>>,
                        indexed_by<"latitude"_n, const_mem_fun<weather, double, &weather::by_latitude>>,
                        indexed_by<"longitude"_n, const_mem_fun<weather, double, &weather::by_longitude>>
    > weather_table_t;

    // TODO: Add state and city table

    TABLE tokensv1 {
        name token_contract;
        string symbol_letters;
        uint8_t precision; // e.g. 4 for Telos , 8 for Kanda

        bool usd_denominated;
        float current_usd_price;

        float miner_amt_per_great_obs;
        float miner_amt_per_good_obs;
        float miner_amt_per_poor_obs;

        float builder_amt_per_great_obs;
        float builder_amt_per_good_obs;
        float builder_amt_per_poor_obs;

        float sponsor_amt_per_great_obs;
        float sponsor_amt_per_good_obs;
        float sponsor_amt_per_poor_obs;

        //int reward_hours;

        auto primary_key() const { return token_contract.value; }
    };
    typedef multi_index<name("tokensv1"), tokensv1> tokens_table_t;

    TABLE minersv1 {
      name miner;
      name token_contract;
      string evm_address;
      float multiplier;
      bool evm_send_enabled;
      float balance;

      auto primary_key() const { return miner.value; }
    };
    typedef multi_index<name("minersv1"), minersv1> miners_table_t;


    TABLE builders {
      name builder;
      name token_contract;
      string evm_address;
      float multiplier;
      bool evm_send_enabled;
      uint16_t number_devices;
      float balance;
      string enclosure_type;

      auto primary_key() const { return builder.value; }
    };
    typedef multi_index<name("builders"), builders> builders_table_t;

    TABLE rewardsv1 {
      name devname;
      name miner;
      name builder;
      name sponsor;

      uint64_t last_miner_payout;
      uint64_t last_builder_payout;
      uint64_t last_sponsor_payout;

      bool picture_sent;
      string recommend_memo;
      float multiplier;

      auto primary_key() const { return devname.value; }
      uint64_t by_miner() const { return miner.value; }
      uint64_t by_builder() const { return builder.value; }
      uint64_t by_sponsor() const { return sponsor.value; }
    };
    typedef multi_index<name("rewardsv1"), rewardsv1> rewards_table_t;

    TABLE flags {
      uint64_t bit_value;
      string processing_step;
      string issue;
      string explanation;

      auto primary_key() const { return bit_value; }
    };
    typedef multi_index<name("flags"), flags> flags_table_t;

};
