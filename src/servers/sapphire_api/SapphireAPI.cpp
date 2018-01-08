#include "SapphireAPI.h"
#include <common/Crypt/base64.h>
#include "Session.h"
#include "PlayerMinimal.h"
#include <time.h>

#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

#include <common/Database/DatabaseDef.h>
#include <boost/make_shared.hpp>

Core::Network::SapphireAPI::SapphireAPI()
{
   
}

Core::Network::SapphireAPI::~SapphireAPI()
{
   
}

bool Core::Network::SapphireAPI::login( const std::string& username, const std::string& pass, std::string& sId )
{
   std::string query = "SELECT account_id FROM accounts WHERE account_name = '" + username + "' AND account_pass = '" + pass + "';";
   
   // check if a user with that name / password exists
   auto pQR = g_charaDb.query( query );
   // found?
   if( !pQR->next() )
      return false;

   // user found, proceed
   uint32_t accountId = pQR->getUInt( 1 );

   // session id string generation
   srand( ( uint32_t )time( NULL ) + 42 );
   uint8_t sid[58];

   for( int32_t i = 0; i < 56; i += 4 )
   {
      short number = 0x1111 + rand() % 0xFFFF;
      sprintf( ( char* )sid + i, "%04hx", number );
   }

   // create session for the new sessionid and store to sessionlist
   auto pSession = boost::make_shared< Session >();
   pSession->setAccountId( accountId );
   pSession->setSessionId( sid );

   //auto ip2 = boost::asio::ip::address::from_string( request->remote_endpoint_address );

   //pSession->setIP( ip2.to_v4().to_ulong() );

   std::stringstream ss;

   for( size_t i = 0; i < 56; i++ )
   {
      ss << std::hex << sid[i];
   }
   m_sessionMap[ ss.str() ] = pSession;
   sId = ss.str();
      
   return true;

}


bool Core::Network::SapphireAPI::insertSession( const uint32_t& accountId, std::string& sId )
{
	// create session for the new sessionid and store to sessionlist
	auto pSession = boost::make_shared< Session >();
	pSession->setAccountId( accountId );
	pSession->setSessionId( (uint8_t *)sId.c_str() );

	m_sessionMap[sId] = pSession;

	return true;

}

bool Core::Network::SapphireAPI::createAccount( const std::string& username, const std::string& pass, std::string& sId )
{
   // get account from login name
   auto pQR = g_charaDb.query( "SELECT account_id FROM accounts WHERE account_name = '" + username + "';" );
   // found?
   if( pQR->next() )
      return false;

   // we are clear and can create a new account
   // get the next free account id
   pQR = g_charaDb.query( "SELECT MAX(account_id) FROM accounts;" );
   if( !pQR->next() )
      return false;
   uint32_t accountId = pQR->getUInt( 1 ) + 1;

   // store the account to the db
   g_charaDb.directExecute( "INSERT INTO accounts (account_Id, account_name, account_pass, account_created) VALUE( " +
                      std::to_string( accountId ) + ", '" +
                      username + "', '" +
                      pass + "', " + 
                      std::to_string( time( nullptr ) ) + ");");

   
   if( !login( username, pass, sId ) )
      return false;

   return true;
 
}

int Core::Network::SapphireAPI::createCharacter( const int& accountId, const std::string& name, const std::string& infoJson, const int& gmRank )
{
   Core::PlayerMinimal newPlayer;

   newPlayer.setAccountId( accountId );
   newPlayer.setId( getNextCharId() );
   newPlayer.setContentId( getNextContentId() );
   newPlayer.setName( name.c_str() );

   boost::property_tree::ptree pt;

   std::stringstream ss;
   ss << infoJson;

   boost::property_tree::read_json( ss, pt );

   const char *ptr = infoJson.c_str() + 50;

   std::string lookPart( ptr );
   int32_t pos = lookPart.find_first_of( "]" );
   if( pos != std::string::npos )
   {
      lookPart = lookPart.substr( 0, pos + 1 );
   }

   std::vector< int32_t > tmpVector;
   std::vector< int32_t > tmpVector2;

   BOOST_FOREACH( boost::property_tree::ptree::value_type &v, pt.get_child( "content" ) )
   {
      boost::property_tree::ptree subtree1 = v.second;
      BOOST_FOREACH( boost::property_tree::ptree::value_type &vs, subtree1 )
      {
         boost::property_tree::ptree subtree2 = vs.second;
         //std::cout << vs.second.data();
         tmpVector.push_back( std::stoi( vs.second.data() ) );
      }
      if( !v.second.data().empty() )
         tmpVector2.push_back( std::stoi( v.second.data() ) );
   }
   std::vector< int32_t >::iterator it = tmpVector.begin();
   for( int32_t i = 0; it != tmpVector.end(); ++it, i++ )
   {
      newPlayer.setLook( i, *it );
   }

   std::string rest = infoJson.substr( pos + 53 );

   newPlayer.setVoice( tmpVector2.at( 0 ) );
   newPlayer.setGuardianDeity( tmpVector2.at( 1 ) );
   newPlayer.setBirthDay( tmpVector2.at( 3 ), tmpVector2.at( 2 ) );
   newPlayer.setClass( tmpVector2.at( 4 ) );
   newPlayer.setTribe( tmpVector2.at( 5 ) );
   newPlayer.setGmRank( gmRank );

   newPlayer.saveAsNew();

   return newPlayer.getAccountId();
}

void Core::Network::SapphireAPI::deleteCharacter( std::string name, uint32_t accountId )
{
   PlayerMinimal deletePlayer;
   auto charList = getCharList( accountId );
   for( uint32_t i = 0; i < charList.size(); i++ )
   {
      PlayerMinimal tmpPlayer = charList.at( i );

      if( tmpPlayer.getName() == name )
      {
         deletePlayer = tmpPlayer;
         break;
      }
   }

   int32_t id = deletePlayer.getId();

   g_charaDb.execute( "DELETE FROM charainfo WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM characlass WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charaglobalitem WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charainfoblacklist WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charainfofriendlist WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charainfolinkshell WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charainfosearch WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charaitemcrystal WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charaiteminventory WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charaitemgearset WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
   g_charaDb.execute( "DELETE FROM charaquestnew WHERE CharacterId LIKE '" + std::to_string( id ) + "';" );
}

std::vector< Core::PlayerMinimal > Core::Network::SapphireAPI::getCharList( uint32_t accountId )
{

   std::vector< Core::PlayerMinimal > charList;

   auto pQR = g_charaDb.query( "SELECT CharacterId, ContentId FROM charainfo WHERE AccountId = " + std::to_string( accountId ) + ";" );

   while( pQR->next() )  
   {
      Core::PlayerMinimal player;

      uint32_t charId = pQR->getUInt( 1 );

      player.load( charId );

      charList.push_back( player );
   } 
   return charList;
}

bool Core::Network::SapphireAPI::checkNameTaken( std::string name )
{
  
   g_charaDb.escapeString( name ); 
   std::string query = "SELECT * FROM charainfo WHERE Name = '" + name + "';";

   auto pQR = g_charaDb.query( query );

   if( !pQR->next() )
      return false;
   else
      return true;
}

uint32_t Core::Network::SapphireAPI::getNextCharId()
{
   uint32_t charId = 0;

   auto pQR = g_charaDb.query( "SELECT MAX(CharacterId) FROM charainfo" );

   if( !pQR->next() )
      return 0x00200001;

   charId = pQR->getUInt( 1 ) + 1;
   if( charId < 0x00200001 )
      return 0x00200001;

   return charId;
}

uint64_t Core::Network::SapphireAPI::getNextContentId()
{
   uint64_t contentId = 0;

   auto pQR = g_charaDb.query( "SELECT MAX(ContentId) FROM charainfo" );

   if( !pQR->next() )
      return 0x0040000001000001;

   contentId = pQR->getUInt64( 1 ) + 1;
   if( contentId < 0x0040000001000001 )
      return 0x0040000001000001;

   return contentId;
}

int Core::Network::SapphireAPI::checkSession( const std::string& sId )
{
   auto it = m_sessionMap.find( sId );

   if( it == m_sessionMap.end() )
      return -1;

   return it->second->getAccountId();
}


bool Core::Network::SapphireAPI::removeSession( const std::string& sId )
{
   auto it = m_sessionMap.find( sId );

   if( it != m_sessionMap.end() )
      m_sessionMap.erase( sId );

   return true;
}