
#ifndef ASYNCTELEGRAM
#define ASYNCTELEGRAM

#include <Arduino.h>
#if defined(ESP32)
	#include <WiFi.h>
	#include <HTTPClient.h>
#elif defined(ESP8266)
	#include <ESP8266WiFi.h>
	#include <WiFiClientSecure.h>
#else
	#error "This library work only with ESP8266 or ESP32"
#endif


#include "DataStructures.h"
#include "InlineKeyboard.h"
#include "ReplyKeyboard.h"

#define DEBUG_MODE       	0 			// enable debugmode -> print debug data on the Serial
#define BUFFER_BIG       	1024 		// json parser buffer size (ArduinoJson v6)
#define BUFFER_SMALL      	256 		// json parser buffer size (ArduinoJson v6)
#define USE_FINGERPRINT  	1 			// use Telegram fingerprint server validation
                                  		// MUST be enabled for ESP8266 Core library > 2.4.2
#define SERVER_TIMEOUT		5000

// Here we store the stuff related to the Telegram server reply
typedef struct {
	bool 		waitingReply = false;
	uint32_t	timestamp;
	String 		payload;

	// Task sharing variables
	// Here we can share data with task for handling the request to server
	String 		command;
	String 		param;

} HttpServerReply;



class AsyncTelegram
{

public:
	// default constructor
	AsyncTelegram();
	// default destructor
	~AsyncTelegram();

	// set the telegram token
	// params
	//   token: the telegram token
	void setTelegramToken(const char* token);

	// use the URL style address "api.telegram.org" or the fixed IP address "149.154.167.198"
	// for all communication with the telegram server
	// Default value is true
	// params
	//   value: true  -> use URL style address
	//          false -> use fixed IP addres
	void useDNS(bool value);

	// enable/disable the UTF8 encoding for the received message.
	// Default value is false (disabled)
	// param
	//   value: true  -> encode the received message with UTF8 encoding rules
	//          false -> leave the received message as-is
	void enableUTF8Encoding(bool value);

	// test the connection between ESP8266 and the telegram server
	// returns
	//    true if no error occurred
	bool begin(void);


	// reset the connection between ESP8266 and the telegram server (ex. when connection was lost)
	// returns
	//    true if no error occurred
	bool reset(void);

	// get the first unread message from the queue (text and query from inline keyboard). 
	// This is a destructive operation: once read, the message will be marked as read
	// so a new getMessage will read the next message (if any).
	// params
	//   message: the data structure that will contains the data retrieved
	// returns
	//   MessageNoData: an error has occurred
	//   MessageText  : the received message is a text
	//   MessageQuery : the received message is a query (from inline keyboards)
	MessageType getNewMessage(TBMessage &message);

	// send a message to the specified telegram user ID
	// params
	//   id      : the telegram recipient user ID 
	//   message : the message to send
	//   keyboard: the inline/reply keyboard (optional)
	//             (in json format or using the inlineKeyboard/ReplyKeyboard class helper)
	
	void sendMessage(int64_t id, String message, String keyboard = "");
	void sendMessage(int64_t id, String message, InlineKeyboard &keyboard);	
	void sendMessage(int64_t id, String message, ReplyKeyboard  &keyboard);

	// terminate a query started by pressing an inlineKeyboard button. The steps are:
	// 1) send a message with an inline keyboard
	// 2) wait for a <message> (getNewMessage) of type MessageQuery
	// 3) handle the query and then call endQuery with <message>.callbackQueryID 
	// params
	//   queryID  : the unique query ID (retrieved with getNewMessage method)
	//   message  : an optional message
	//   alertMode: false -> a simply popup message
	//              true --> an alert message with ok button
	void endQuery(String queryID, String message = "", bool alertMode = false);

	// remove an active reply keyboard for a selected user, sending a message
	// params:
	//   id       : the telegram user ID 
	//   message  : the message to be show to the selected user ID
	//   selective: enable selective mode (hide the keyboard for specific users only)
	//              Targets: 1) users that are @mentioned in the text of the Message object; 
	//                       2) if the bot's message is a reply (has reply_to_message_id), sender of the original message
	// return:
	//   true if no error occurred
	void removeReplyKeyboard(int64_t id, String message, bool selective = false);

	// set the new Telegram API server fingerprint overwriting the default one.
	// It can be obtained by this service: https://www.grc.com/fingerprints.htm
	// quering api.telegram.org
	// params:
	//    newFingerprint: the array of 20 bytes that contains the new fingerprint
	void setFingerprint(const uint8_t *newFingerprint);


	// set the interval in milliseconds for polling 
	// in order to Avoid query Telegram server to much often (ms)
	// params:
	//    pollingTime: interval time in milliseconds
	void setUpdateTime(uint32_t pollingTime);


private:
	char*     m_token;
	int32_t   m_lastUpdate = 0;
	uint32_t  m_lastUpdateTime;
	uint32_t  m_minUpdateTime = 2000;

	bool      m_useDNS = false;
	bool      m_UTF8Encoding = false;	
	uint8_t   m_fingerprint[20];

	// Struct for store telegram server reply and infos about it
	HttpServerReply httpData;

#if defined(ESP32) 
	WiFiClientSecure telegramClient;
#elif USE_FINGERPRINT == 0 
	WiFiClientSecure telegramClient;	
#elif defined(ESP8266) 
	BearSSL::WiFiClientSecure telegramClient;	
#endif	

	// send commands to the telegram server. For info about commands, check the telegram api https://core.telegram.org/bots/api
	// params
	//   command   : the command to send, i.e. getMe
	//   parameters: optional parameters
	// returns
	//   an empty string if error
	//   a string containing the Telegram JSON response
	String postCommand(const char* const& command, const char* const& param, bool blocking = false);


	/*  postCommand() must be a blocking function. It will send an http request to server and wait for reply.
		Keeping connection with server opened, we can save a lot of time but with ESP32 we can start
		the task on the other core for a full async http handling, so the job will be handled in a different manner.

		Instead using a WiFiClientSecure object, like with ESP82266 function, we can use HTTPClient class that, 
		due to the setReuse() property, can save a lot of time (With ESP8266 unlucky don't work as expected)
	*/
	static void httpPostTask(void *args);

	// helper function used to select the properly working mode with ESP8266/ESP32
	void sendCommand(const char* const&  command, const char* const& param);

	// get some information about the bot
	// params
	//   user: the data structure that will contains the data retreived
	// returns
	//   true if no error occurred
	bool getMe(TBUser &user);

	bool getUpdates();

	bool checkConnection();

	bool serverReply(const char* const&  replyMsg);

};


#endif



