#ifndef PsychicHttp_h
#define PsychicHttp_h

#define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_WARN
#define PH_TAG "http"

#define MAX_COOKIE_SIZE 256
#define FILE_CHUNK_SIZE 8*1024
#define MAX_UPLOAD_SIZE   (200*1024) // 200 KB
#define MAX_UPLOAD_SIZE_STR "200KB"

//#define ENABLE_KEEPALIVE
#define ENABLE_SERVE_STATIC

#include <ArduinoTrace.h>
#include <esp_https_server.h>
#include <http_status.h>
#include <map>
#include <list>
#include <libb64/cencode.h>
#include "esp_random.h"
#include "MD5Builder.h"
#include <UrlEncode.h>
#include "FS.h"

#ifdef ENABLE_KEEPALIVE
  #include <keep_alive.h>
#endif

typedef std::map<String, String> SessionData;

struct HTTPHeader {
  char * field;
  char * value;
};

//TODO: not quite used yet.
struct MultipartContent {
  char * content_type;
  char * name;
  char * filename;
};

enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };

//used for async message sending
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
    char *data;
};

class PsychicHttpServer;
class PsychicHttpServerRequest;
class PsychicHttpServerResponse;
class PsychicHttpWebSocketRequest;
class PsychicHttpWebSocketConnection;
class PsychicStaticFileHandler;

/*
 * PARAMETER :: Chainable object to hold GET/POST and FILE parameters
 * */

// class PsychicWebParameter {
//   private:
//     String _name;
//     String _value;
//     size_t _size;
//     bool _isForm;
//     bool _isFile;

//   public:

//     PsychicWebParameter(const String& name, const String& value, bool form=false, bool file=false, size_t size=0): _name(name), _value(value), _size(size), _isForm(form), _isFile(file){}
//     const String& name() const { return _name; }
//     const String& value() const { return _value; }
//     size_t size() const { return _size; }
//     bool isPost() const { return _isForm; }
//     bool isFile() const { return _isFile; }
// };

/*
 * HEADER :: Chainable object to hold the headers
 * */

// class PsychichWebHeader {
//   private:
//     String _name;
//     String _value;

//   public:
//     PsychichWebHeader(const String& name, const String& value): _name(name), _value(value){}
//     PsychichWebHeader(const String& data): _name(), _value(){
//       if(!data) return;
//       int index = data.indexOf(':');
//       if (index < 0) return;
//       _name = data.substring(0, index);
//       _value = data.substring(index + 2);
//     }
//     ~PsychichWebHeader(){}
//     const String& name() const { return _name; }
//     const String& value() const { return _value; }
//     String toString() const { return String(_name+": "+_value+"\r\n"); }
// };

class PsychicHttpServerRequest {
  friend PsychicHttpServer;

  protected:
    PsychicHttpServer *_server;
    http_method _method;
    String _uri;
    String _query;
    String _body;
    SessionData *_session;

  public:
    PsychicHttpServerRequest(PsychicHttpServer *server, httpd_req_t *req);
    virtual ~PsychicHttpServerRequest();

    httpd_req_t *_req;

    esp_err_t loadBody();

    virtual bool isUpload() { return false; }
    virtual bool isWebSocket() { return false; }

    //int headers();
    const String header(const char *name);
    bool hasHeader(const char *name);

    static void freeSession(void *ctx);
    bool hasSessionKey(const String& key);
    const String getSessionKey(const String& key);
    void setSessionKey(const String& key, const String& value);

    bool hasCookie(const char * key);
    const String getCookie(const char * key);

    http_method method();
    const String methodStr();
    const String& uri();
    const String& url() { return uri(); }
    const String host();
    const String contentType();
    size_t contentLength();
    const String& body();
    bool multipart();

    const String queryString();
    bool hasParam(const char *key);
    const String getParam(const char *name);

    const String _extractParam(const String& authReq, const String& param, const char delimit);
    const String _getRandomHexString();
    bool authenticate(const char * username, const char * password);
    esp_err_t requestAuthentication(HTTPAuthMethod mode, const char* realm, const String& authFailMsg);

    esp_err_t redirect(const char *url);
    esp_err_t reply(int code);
    esp_err_t reply(const char *content);
    esp_err_t reply(int code, const char *content);
    esp_err_t reply(int code, const char *contentType, const char *content="");
};

class PsychicHttpServerResponse
{
  protected:
    PsychicHttpServerRequest *_request;
    int64_t _contentLength;
    char _status[60];
    const char * body;

    std::list<HTTPHeader> headers;

  public:
    PsychicHttpServerResponse(PsychicHttpServerRequest *request);
    virtual ~PsychicHttpServerResponse();

    void setCode(int code);

    void setContentType(const char *contentType);
    void setContentLength(int64_t contentLength) { _contentLength = contentLength; }
    int64_t getContentLength(int64_t contentLength) { return _contentLength; }

    void addHeader(const char *field, const char *value);
    void setCookie(const char *key, const char *value, unsigned long max_age = 60*60*24*30);

    void setContent(const char *content);
    void setContent(const uint8_t *content, size_t len);

    const char * getContent();
    size_t getContentLength();

    esp_err_t send();
};

typedef std::function<esp_err_t(PsychicHttpServerRequest *request)> PsychicHttpRequestHandler;
typedef std::function<esp_err_t(PsychicHttpServerRequest *request, const String& filename, uint64_t index, uint8_t *data, size_t len)> PsychicHttpBasicUploadHandler;
typedef std::function<esp_err_t(PsychicHttpServerRequest *request, const String& filename, uint64_t index, uint8_t *data, size_t len)> PsychicHttpMultipartUploadHandler;
typedef std::function<esp_err_t(PsychicHttpWebSocketRequest *connection)> PsychicHttpWebSocketRequestHandler;
typedef std::function<esp_err_t(PsychicHttpWebSocketRequest *connection, httpd_ws_frame *frame)> PsychicHttpWebSocketFrameHandler;
typedef std::function<esp_err_t(httpd_handle_t hd, int sockfd)> PsychicHttpOpenHandler;
typedef std::function<esp_err_t(httpd_handle_t hd, int sockfd)> PsychicHttpCloseHandler;

class PsychicHttpServerEndpoint
{
  friend PsychicHttpServer;

  private:
    PsychicHttpServer *server;
    http_method method;

    PsychicHttpRequestHandler request;
    PsychicHttpBasicUploadHandler upload;
    PsychicHttpMultipartUploadHandler multipart;
    PsychicHttpWebSocketRequestHandler wsConnect;
    PsychicHttpWebSocketFrameHandler wsFrame;

  public:
    PsychicHttpServerEndpoint();
    PsychicHttpServerEndpoint(PsychicHttpServer *server, http_method method);

    PsychicHttpServerEndpoint *onRequest(PsychicHttpRequestHandler handler);
    PsychicHttpServerEndpoint *onUpload(PsychicHttpBasicUploadHandler handler);
    PsychicHttpServerEndpoint *onMultipart(PsychicHttpMultipartUploadHandler handler);
    PsychicHttpServerEndpoint *onConnect(PsychicHttpWebSocketRequestHandler handler);
    PsychicHttpServerEndpoint *onFrame(PsychicHttpWebSocketFrameHandler handler);

    static esp_err_t requestHandler(httpd_req_t *req);
    static esp_err_t uploadHandler(httpd_req_t *req);
    static esp_err_t websocketHandler(httpd_req_t *req);
};

class PsychicHttpWebSocketRequest : public PsychicHttpServerRequest
{
  friend PsychicHttpServer;

  public:
    PsychicHttpWebSocketRequest(PsychicHttpServer *server, httpd_req_t *req);
    virtual ~PsychicHttpWebSocketRequest();

    PsychicHttpWebSocketConnection *connection;

    esp_err_t reply(httpd_ws_frame_t * ws_pkt);
    esp_err_t reply(httpd_ws_type_t op, const void *data, size_t len);
    esp_err_t reply(const char *buf);
};

class PsychicHttpWebSocketConnection
{
  friend PsychicHttpServer;

  protected:
    httpd_handle_t _server;
    int _fd;

  public:
    PsychicHttpWebSocketConnection(httpd_handle_t server, int fd);

    esp_err_t queueMessage(httpd_ws_frame_t * ws_pkt);
    esp_err_t queueMessage(httpd_ws_type_t op, const void *data, size_t len);
    esp_err_t queueMessage(const char *buf);
    static void queueMessageCallback(void *arg);

    int id() { return this->_fd; }
};

class PsychicHttpServer
{
  protected:
    bool use_ssl = false;
    std::list<PsychicHttpServerEndpoint *> endpoints;

  public:
    PsychicHttpServer();
    ~PsychicHttpServer();

    //esp-idf specific stuff
    httpd_handle_t server;
    httpd_config_t config;
    httpd_ssl_config_t ssl_config;

    //some limits on what we will accept
    unsigned long maxUploadSize = 200 * 1024;
    unsigned long maxRequestBodySize = 16 * 1024;

    #ifdef ENABLE_KEEPALIVE
      wss_keep_alive_config_t keep_alive_config;
      wss_keep_alive_t keep_alive;
    #endif
    
    PsychicHttpServerEndpoint defaultEndpoint;
    PsychicHttpOpenHandler openHandler;
    PsychicHttpCloseHandler closeHandler;

    static void destroy(void *ctx);

    void listen(uint16_t port);
    void listen(uint16_t port, const char *cert, const char *private_key);
    bool start();
    void stop();

    PsychicHttpServerEndpoint *on(const char* uri);
    PsychicHttpServerEndpoint *on(const char* uri, http_method method);
    PsychicHttpServerEndpoint *on(const char* uri, PsychicHttpRequestHandler onRequest);
    PsychicHttpServerEndpoint *on(const char* uri, http_method method, PsychicHttpRequestHandler onRequest);

    PsychicHttpServerEndpoint *websocket(const char* uri);

    void onNotFound(PsychicHttpRequestHandler fn);
    static esp_err_t notFoundHandler(httpd_req_t *req, httpd_err_code_t err);
    static esp_err_t defaultNotFoundHandler(PsychicHttpServerRequest *request);

    void onOpen(PsychicHttpOpenHandler handler);
    void onClose(PsychicHttpCloseHandler handler);
    static esp_err_t openCallback(httpd_handle_t hd, int sockfd);
    static void closeCallback(httpd_handle_t hd, int sockfd);

    PsychicStaticFileHandler *staticHandler;
    PsychicStaticFileHandler& serveStatic(const char* uri, fs::FS& fs, const char* path, const char* cache_control = NULL);

    void sendAll(httpd_ws_frame_t * ws_pkt);
    void sendAll(httpd_ws_type_t op, const void *data, size_t len);
    void sendAll(const char *buf);
};

class PsychicStaticFileHandler {
  using File = fs::File;
  using FS = fs::FS;
  private:
    bool _getFile(PsychicHttpServerRequest *request);
    bool _fileExists(const String& path);
    uint8_t _countBits(const uint8_t value) const;
  protected:
    FS _fs;
    File _file;
    String _filename;
    String _uri;
    String _path;
    String _default_file;
    String _cache_control;
    String _last_modified;
    bool _isDir;
    bool _gzipFirst;
    uint8_t _gzipStats;
  public:
    PsychicStaticFileHandler(const char* uri, FS& fs, const char* path, const char* cache_control);
    bool canHandle(PsychicHttpServerRequest *request);
    esp_err_t handleRequest(PsychicHttpServerRequest *request);
    PsychicStaticFileHandler& setIsDir(bool isDir);
    PsychicStaticFileHandler& setDefaultFile(const char* filename);
    PsychicStaticFileHandler& setCacheControl(const char* cache_control);
    PsychicStaticFileHandler& setLastModified(const char* last_modified);
    PsychicStaticFileHandler& setLastModified(struct tm* last_modified);
  #ifdef ESP8266
    PsychicStaticFileHandler& setLastModified(time_t last_modified);
    PsychicStaticFileHandler& setLastModified(); //sets to current time. Make sure sntp is runing and time is updated
  #endif
    //PsychicStaticFileHandler& setTemplateProcessor(AwsTemplateProcessor newCallback) {_callback = newCallback; return *this;}
};

class PsychicHttpFileResponse: public PsychicHttpServerResponse
{
  using File = fs::File;
  using FS = fs::FS;
  private:
    File _content;
    String _path;
    bool _sendContentLength;
    bool _chunked;
    String _contentType;
    void _setContentType(const String& path);
  public:
    PsychicHttpFileResponse(PsychicHttpServerRequest *request, FS &fs, const String& path, const String& contentType=String(), bool download=false);
    PsychicHttpFileResponse(PsychicHttpServerRequest *request, File content, const String& path, const String& contentType=String(), bool download=false);
    ~PsychicHttpFileResponse();
    esp_err_t send();
};

#endif /* PsychicHttp_h */