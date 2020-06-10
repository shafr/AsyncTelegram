#define ARDUINOJSON_USE_LONG_LONG 1 // for using int_64 data
#include <ArduinoJson.h>
#if defined(ESP32)
	#include <WiFi.h>
#elif defined(ESP8266)
	#include <ESP8266WiFi.h>
#else
	#error "This library work only with ESP8266 or ESP32"
#endif
#include <WiFiClientSecure.h>
#include "CTBot.h"
#include "Utilities.h"

#define TELEGRAM_URL  "api.telegram.org"
#define TELEGRAM_IP   "149.154.167.220" // "149.154.167.198" <-- Old IP
#define TELEGRAM_PORT 443
// get fingerprints from https://www.grc.com/fingerprints.htm
const uint8_t fingerprint[20] = { 0xF2, 0xAD, 0x29, 0x9C, 0x34, 0x48, 0xDD, 0x8D, 0xF4, 0xCF, 0x52, 0x32, 0xF6, 0x57, 0x33, 0x68, 0x2E, 0x81, 0xC1, 0x90 };


#if CTBOT_DEBUG_MODE > 0
inline void CTBot::serialLog(String message) const {
	Serial.print(message);
}
#else
inline void CTBot::serialLog(String) const {}
#endif

CTBot::CTBot() {
	m_wifiConnectionTries = 0;  // wait until connection to the AP is established (locking!)
	m_statusPin           = CTBOT_DISABLE_STATUS_PIN; // status pin disabled
	m_token               = ""; // no token
	m_lastUpdate          = 0;  // not updated yet
	m_useDNS              = false; // use static IP for Telegram Server
	m_UTF8Encoding        = false; // no UTF8 encoded string conversion
	setFingerprint(fingerprint);   // set the default fingerprint
}

CTBot::~CTBot() {};



String CTBot::sendCommand(String command, String parameters)
{
#if defined(ESP32)
	WiFiClientSecure telegramServer;
	//telegramServer.setFingerprint(m_fingerprint);
#elif CTBOT_USE_FINGERPRINT == 0 
	WiFiClientSecure telegramServer;
#else
	BearSSL::WiFiClientSecure telegramServer;
	telegramServer.setFingerprint(m_fingerprint);
#endif	

	// check for using symbolic URLs
	if (m_useDNS) {
		// try to connect with URL
		if (!telegramServer.connect(TELEGRAM_URL, TELEGRAM_PORT)) {
			// no way, try to connect with fixed IP
			IPAddress telegramServerIP;
			telegramServerIP.fromString(TELEGRAM_IP);
			if (!telegramServer.connect(telegramServerIP, TELEGRAM_PORT)) {
				serialLog("\nUnable to connect to Telegram server\n");
				return "";
			}
			else {
				serialLog("\nConnected using fixed IP\n");
				useDNS(false);
			}
		}
		else {
			serialLog("\nConnected using DNS\n");
		}

	}
	else {
		// try to connect with fixed IP
		IPAddress telegramServerIP; // (149, 154, 167, 198);
		telegramServerIP.fromString(TELEGRAM_IP);
		if (!telegramServer.connect(telegramServerIP, TELEGRAM_PORT)) {
			serialLog("\nUnable to connect to Telegram server\n");
			return "";
		}
		else
			serialLog("\nConnected using fixed IP\n");
	}

	if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
		digitalWrite(m_statusPin, !digitalRead(m_statusPin));     // set pin to the opposite state

	// must filter command + parameters from escape sequences and spaces
	//	const String URL = "GET /bot" + m_token + (String)"/" + toURL(command + parameters);
	String URL;
	URL.reserve(MAX_STRING_SIZE);
	URL = "GET /bot";
	URL += m_token;
	URL += "/" ;
	URL += command;
	URL += parameters;

	//Serial.println(URL);

	// send the HTTP request
	telegramServer.println(URL);

	if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
		digitalWrite(m_statusPin, !digitalRead(m_statusPin));     // set pin to the opposite state

#if CTBOT_CHECK_JSON == 0
	return telegramServer.readString();
#else

	String response;
	response.reserve(MAX_STRING_SIZE);
	int curlyCounter = -1; // count the open/closed curly bracket for identify the json
	bool skipCounter = false; // for filtering curly bracket inside a text message
	int c;

	while (telegramServer.connected()) {
		while (telegramServer.available()) {
			c = telegramServer.read();
			response += (char)c;
			if (c == '\\') {
				// escape character -> read next and skip
				c = telegramServer.read();
				response += (char)c;
				continue;
			}
			if (c == '"')
				skipCounter = !skipCounter;
			if (!skipCounter) {
				if (c == '{') {
					if (curlyCounter == -1)
						curlyCounter = 1;
					else
						curlyCounter++;
				}
				else if (c == '}')
					curlyCounter--;
				if (curlyCounter == 0) {

					// JSON ended, close connection and return JSON
					telegramServer.flush();
					telegramServer.stop();
					return response;
				}
			}
		}
	}

	// timeout, no JSON to parse
	telegramServer.flush();
	telegramServer.stop();
	return "";
#endif
}

String CTBot::toUTF8(String message) const
{
	String converted;
	converted.reserve(MAX_STRING_SIZE);
	uint16_t i = 0;
	while (i < message.length()) {
		String subMessage(message[i]);
		if (message[i] != '\\') {
			converted += subMessage;
			i++;
		} else {
			// found "\"
			i++;
			if (i == message.length()) {
				// no more characters
				converted += subMessage;
			} else {
				subMessage += (String)message[i];
				if (message[i] != 'u') {
					converted += subMessage;
					i++;
				} else {
					//found \u escape code
					i++;
					if (i == message.length()) {
						// no more characters
						converted += subMessage;
					} else {
						uint8_t j = 0;
						while ((j < 4) && ((j + i) < message.length())) {
							subMessage += (String)message[i + j];
							j++;
						}
						i += j;
						String utf8;
						if (unicodeToUTF8(subMessage, utf8))
							converted += utf8;
						else
							converted += subMessage;
					}
				}
			}
		}
	}
	return converted;
}

void CTBot::useDNS(bool value)
{	m_useDNS = value; }

void CTBot::enableUTF8Encoding(bool value) 
{	m_UTF8Encoding = value;}

void CTBot::setMaxConnectionRetries(uint8_t retries)
{	m_wifiConnectionTries = retries;}

void CTBot::setStatusPin(int8_t pin)
{	m_statusPin = pin;}

void CTBot::setTelegramToken(String token)
{	m_token = token;}


bool CTBot::testConnection(){
	TBUser user;
	return getMe(user);
}


bool CTBot::getMe(TBUser &user) {
	String response;
	response.reserve(MAX_STRING_SIZE);
	response = sendCommand("getMe");
	if (response.length() == 0)
		return false;

#if CTBOT_BUFFER_SIZE > 0
	DynamicJsonDocument root(CTBOT_BUFFER_SIZE_SMALL);
#else
	#pragma message  "CTBOT_BUFFER_SIZE_SMALL MUST BE > 0"
#endif
	deserializeJson(root, response);

	bool ok = root["ok"];
	if (!ok) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("getMe error:");
		serializeJson(root, Serial);
		serialLog("\n");
#endif
		return false;
	}

#if CTBOT_DEBUG_MODE > 0
	serialLog("getMe message:\n");
	serializeJson(root, Serial);
	serialLog("\n");
#endif

	user.id           = root["result"]["id"];
	user.isBot        = root["result"]["is_bot"];
	user.firstName    = root["result"]["first_name"];
	user.username     = root["result"]["username"];
	user.lastName     = root["result"]["last_name"];
	user.languageCode = root["result"]["language_code"];
	return true;
}




CTBotMessageType CTBot::getNewMessage(TBMessage &message) {

	message.messageType = CTBotMessageNoData;

	if(! (millis() - m_lastUpdateTime > SERVER_MIN_UPDATE_TIME))
		return message.messageType;

	char buf[21];
	ltoa(m_lastUpdate, buf, 10);
	// polling timeout: add &timeout=<seconds>
	// default is zero (short polling).
	String parameters;
	parameters.reserve(MAX_STRING_SIZE);
	parameters = "?limit=1&allowed_updates=message,callback_query";
	if (m_lastUpdate != 0) {
		parameters += "&offset=";
		parameters += (String)buf;
	}
		
	String response;
	response.reserve(MAX_STRING_SIZE);
	response = sendCommand("getUpdates", parameters);

	if (response.length() == 0) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("getNewMessage error: response with no data\n");
#endif
		return CTBotMessageNoData;
	}

#if CTBOT_BUFFER_SIZE > 0
	DynamicJsonDocument root(CTBOT_BUFFER_SIZE);
#else
	#pragma message  "CTBOT_BUFFER_SIZE MUST BE > 0"
#endif

	if (m_UTF8Encoding)
		response = toUTF8(response);

	deserializeJson(root, response);
	//Serial.println(response);

	bool ok = root["ok"];
	if (!ok) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("getNewMessage error: ");
		serializeJsonPretty(root, Serial);
		serialLog("\n");
#endif
		return CTBotMessageNoData;
	}

#if CTBOT_DEBUG_MODE > 0
	serialLog("getNewMessage JSON: \n");
	serializeJsonPretty(root, Serial);
	serialLog("\n");
#endif

	uint32_t updateID = root["result"][0]["update_id"];
	if (updateID == 0){
		return CTBotMessageNoData;
	}
	m_lastUpdate = updateID + 1;
	m_lastUpdateTime = millis();
	
	if(root["result"][0]["callback_query"]["id"]){
		// this is a callback query
		message.callbackQueryID   = root["result"][0]["callback_query"]["id"];
		message.sender.id         = root["result"][0]["callback_query"]["from"]["id"];
		message.sender.username   = root["result"][0]["callback_query"]["from"]["username"];
		message.sender.firstName  = root["result"][0]["callback_query"]["from"]["first_name"];
		message.sender.lastName   = root["result"][0]["callback_query"]["from"]["last_name"];
		message.messageID         = root["result"][0]["callback_query"]["message"]["message_id"];
		message.text              = root["result"][0]["callback_query"]["message"]["text"];
		message.date              = root["result"][0]["callback_query"]["message"]["date"];
		message.chatInstance      = root["result"][0]["callback_query"]["chat_instance"];
		message.callbackQueryData = root["result"][0]["callback_query"]["data"];
		message.messageType       = CTBotMessageQuery;
		return CTBotMessageQuery;
	}	
	else if(root["result"][0]["message"]["message_id"]){
		// this is a message
		message.messageID        = root["result"][0]["message"]["message_id"];
		message.sender.id        = root["result"][0]["message"]["from"]["id"];
		message.sender.username  = root["result"][0]["message"]["from"]["username"];
		message.sender.firstName = root["result"][0]["message"]["from"]["first_name"];
		message.sender.lastName  = root["result"][0]["message"]["from"]["last_name"];
		message.group.id         = root["result"][0]["message"]["chat"]["id"];
		message.group.title      = root["result"][0]["message"]["chat"]["title"];
		message.date             = root["result"][0]["message"]["date"];
			    
		if(root["result"][0]["message"]["location"]){
			// this is a location message
			message.location.longitude = root["result"][0]["message"]["location"]["longitude"];
			message.location.latitude = root["result"][0]["message"]["location"]["latitude"];
			message.messageType = CTBotMessageLocation;
			return CTBotMessageLocation;
		}		
		else if(root["result"][0]["message"]["contact"]){
			// this is a contact message
			message.contact.id          = root["result"][0]["message"]["contact"]["user_id"];
			message.contact.firstName   = root["result"][0]["message"]["contact"]["first_name"];
			message.contact.lastName    = root["result"][0]["message"]["contact"]["last_name"];
			message.contact.phoneNumber = root["result"][0]["message"]["contact"]["phone_number"];
			message.contact.vCard       = root["result"][0]["message"]["contact"]["vcard"];
			message.messageType = CTBotMessageContact;
			return CTBotMessageContact;
		}		
		else if (root["result"][0]["message"]["text"]) {
			// this is a text message
		    message.text        = root["result"][0]["message"]["text"];		    
			message.messageType = CTBotMessageText;
			return CTBotMessageText;
		}
	}
	// no valid/handled message
	return CTBotMessageNoData;
}

bool CTBot::sendMessage(int64_t id, String message, String keyboard)
{
	if (message.length() == 0)
		return false;
	String 	parameters;
	parameters.reserve(MAX_STRING_SIZE);

	parameters = "?chat_id=";
	parameters += int64ToAscii(id) ;
	parameters += "&text=";
	parameters +=  URLEncodeMessage(message);

	if (keyboard.length() != 0) {
		parameters += "&reply_markup=" ;
		parameters += keyboard;
	}		

	String response = sendCommand("sendMessage", parameters);

	if (response.length() == 0) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("SendMessage error: response with no data\n");
#endif
		return false;
	}

#if CTBOT_BUFFER_SIZE > 0
	DynamicJsonDocument root(CTBOT_BUFFER_SIZE);
#else
	#pragma message  "CTBOT_BUFFER_SIZE MUST BE > 0"
#endif
 	deserializeJson(root, response);

	bool ok = root["ok"];
	if (!ok) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("SendMessage error: ");
		serializeJsonPretty(root, Serial);
		serialLog("\n");
#endif
		return false;
	}

#if CTBOT_DEBUG_MODE > 0
	serialLog("SendMessage JSON: ");
	serializeJsonPretty(root, Serial);
	serialLog("\n");
#endif

	return true;
}



bool CTBot::sendMessage(int64_t id, String message, CTBotInlineKeyboard &keyboard) {
	return sendMessage(id, message, keyboard.getJSON());
}


bool CTBot::sendMessage(int64_t id, String message, CTBotReplyKeyboard &keyboard) {
	return sendMessage(id, message, keyboard.getJSON());
}



bool CTBot::endQuery(String queryID, String message, bool alertMode)
{
	if (0 == queryID.length())
		return false;

	String 	parameters = "?callback_query_id=";
			parameters += queryID;

	if (message.length() != 0) {

		if (alertMode) {
			parameters += "&text=";
			parameters += URLEncodeMessage(message); ;
			parameters += "&show_alert=true";
		}			
		else {
			parameters += "&text=" ;
			parameters += URLEncodeMessage(message); ;
			parameters += "&show_alert=false";
		}
	}

	String response = sendCommand("answerCallbackQuery", parameters);
	if (response.length() == 0)
		return false;

#if CTBOT_BUFFER_SIZE > 0
	DynamicJsonDocument root(CTBOT_BUFFER_SIZE);
#else
	#pragma message  "CTBOT_BUFFER_SIZE MUST BE > 0"
#endif
	deserializeJson(root, response);

	bool ok = root["ok"];
	if (!ok) {
#if CTBOT_DEBUG_MODE > 0
		serialLog("answerCallbackQuery error:");
		serializeJsonPretty(root, Serial);
		serialLog("\n");
#endif
		return false;
	}

#if CTBOT_DEBUG_MODE > 0
	serializeJson(root, Serial);
	serialLog("\n");
#endif

	return true;
}

bool CTBot::removeReplyKeyboard(int64_t id, String message, bool selective)
{
	DynamicJsonDocument root(CTBOT_BUFFER_SIZE_SMALL);
	String command;
	root["remove_keyboard"] = true;
	if (selective) {
		root["selective"] = true;
	}
	serializeJson(root, command);
	return sendMessage(id, message, command);
}

void CTBot::setFingerprint(const uint8_t * newFingerprint)
{
	for (int i = 0; i < 20; i++)
		m_fingerprint[i] = newFingerprint[i];
}

bool CTBot::setIP(String ip, String gateway, String subnetMask, String dns1, String dns2) const {
	IPAddress IP, SN, GW, DNS1, DNS2;

	if (!IP.fromString(ip)) {
		serialLog("--- setIP: error on IP address\n");
		return false;
	}
	if (!SN.fromString(subnetMask)) {
		serialLog("--- setIP: error on subnet mask\n");
		return false;
	}
	if (!GW.fromString(gateway)) {
		serialLog("--- setIP: error on gateway address\n");
		return false;
	}
	if (dns1.length() != 0) {
		if (!DNS1.fromString(dns1)) {
			serialLog("--- setIP: error on DNS1 address\n");
			return false;
		}
	}
	if (dns2.length() != 0) {
		if (!DNS2.fromString(dns2)) {
			serialLog("--- setIP: error on DNS1 address\n");
			return false;
		}
	}
	if (WiFi.config(IP, GW, SN, DNS1, DNS2))
		return true;
	else {
		serialLog("--- setIP: error on setting the static ip address (WiFi.config)\n");
		return false;
	}
}

bool CTBot::wifiConnect(String ssid, String password) const
{
	// attempt to connect to Wifi network:
	int tries = 0;
	String message = "\n\nConnecting Wifi: " ;
		   message += ssid ;
		   message += "\n";
	serialLog(message);

#if CTBOT_STATION_MODE > 0
	WiFi.mode(WIFI_STA);
#else
	WiFi.mode(WIFI_AP_STA);
#endif
	delay(500);

	WiFi.begin(ssid.c_str(), password.c_str());
	delay(500);

	if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
		pinMode(m_statusPin, OUTPUT);

	if (0 == m_wifiConnectionTries)
		tries = -1;

	while ((WiFi.status() != WL_CONNECTED) && (tries < m_wifiConnectionTries)) {
		serialLog(".");
		if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
			digitalWrite(m_statusPin, !digitalRead(m_statusPin));     // set pin to the opposite state
		delay(500);
		if (m_wifiConnectionTries != 0) tries++;
	}

	if (WiFi.status() == WL_CONNECTED) {
		IPAddress ip = WiFi.localIP();
		message = "\nWiFi connected\nIP address: " ;
		message += ip.toString() ;
		message += "\n";
		serialLog(message);
		if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
			digitalWrite(m_statusPin, LOW);
		return true;
	}
	else {
		message = "\nUnable to connect to ";
		message += ssid ;
		message += " network.\n";
		serialLog(message);
		if (m_statusPin != CTBOT_DISABLE_STATUS_PIN)
			 digitalWrite(m_statusPin, HIGH);
		return false;
	}
}

