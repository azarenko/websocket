#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <signal.h>
#include <libconfig.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/types.h>
#include <locale.h>
#include <libpq-fe.h>
#include <libwebsockets.h>>

#include "cmdparam.h"
#include "settings.h"
#include "sockutils.h"
#include "proto.h"

#define MAX_CONTENT_LEN 1024 * 512

static int destroy_flag = 0;

/*
 * Terminate server
 */
static void terminate(void)
{
    destroy_flag = 1;
}

/**
 * @brief ...
 * 
 * @param signal ...
 * @return void
 */
static void sighandler(int signal) {
    switch(signal)
    {
        case SIGTERM: 
            syslog(LOG_INFO, "Received signal terminate.");
            terminate();
            break;
        case SIGHUP:
            syslog(LOG_INFO, "Received signal hangup.");
            if(readSettings(configFilePath) == EXIT_FAILURE)
            {
                syslog(LOG_ERR, "Configuration file error.");                
            }
            break;
    }
}
 
/*
 void httpserver_ProcessRequest(struct evhttp_request *req) {
    
    struct evbuffer *inbuff;    
    char content[MAX_CONTENT_LEN];
    int contentLen = 0;
    bzero(content, MAX_CONTENT_LEN);
    
    inbuff = evhttp_request_get_input_buffer(req);    
	while (evbuffer_get_length(inbuff)) 
    {
	contentLen += evbuffer_remove(inbuff, content + contentLen, 128);		
    }
      
    struct evbuffer *outbuff = evbuffer_new();
    
    char *client_ip;
    unsigned short client_port;

    evhttp_connection_get_peer(evhttp_request_get_connection(req), &client_ip, &client_port);
    
    char* responceMessage;
    int responceLength;
    
    int statuscode = proto(content, contentLen, &responceMessage);        
    
    evbuffer_add_printf(outbuff, "%s", responceMessage);
    
    free(responceMessage);
          
    char outcontentLengthStr[3];
    sprintf(outcontentLengthStr, "%zu", evbuffer_get_length(outbuff));
    
    // Set HTTP headers    
    evhttp_add_header(req->output_headers, "Content-Type", "application/json");
    evhttp_add_header(req->output_headers, "Connection", "close");
    evhttp_add_header(req->output_headers, "Content-Length", outcontentLengthStr);

    // Send reply
    evhttp_send_reply(req, statuscode, "OK", outbuff);

    // Free memory
    evbuffer_free(outbuff);    
}
*/

 int websocket_write(struct lws *wsi_in, char *content, int str_size_in) 
{
    if (content == NULL || wsi_in == NULL)
        return -1;

    int n;
    int contentLen;
    char *out = NULL;

    if (str_size_in < 1) 
        contentLen = strlen(content);
    else
        contentLen = str_size_in;

    char* responceMessage;
    int responceLength = proto(content, contentLen, &responceMessage);        
    
    out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + responceLength + LWS_SEND_BUFFER_POST_PADDING));
    
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, responceMessage, responceLength );
    
    free(responceMessage);
    
    n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, responceLength, LWS_WRITE_TEXT);

    free(out);

    return n;
}


static int ws_service_callback(
                         struct lws *wsi,
                         enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{

    switch (reason) {

        case LWS_CALLBACK_ESTABLISHED:
            break;

        case LWS_CALLBACK_RECEIVE:
            websocket_write(wsi ,(char *)in, -1);
            break;
	    
	case LWS_CALLBACK_CLOSED:
	    break;

	default:
		break;
    }

    return 0;
}

struct per_session_data 
{
    int fd;
};
 
 
/**
 * @brief ...
 * 
 * @param argc ...
 * @param argv ...
 * @return int
 */
int main(int argc, char **argv)
{    
    setlocale(LC_ALL, "UTF-8");
    /*
     * init syslog
     */
    openlog(program_invocation_short_name, 0, LOG_USER);
    syslog(LOG_INFO, "Starting.");
    
    /*
     * parse command line params
     */
    if(getCmdParam(argc, argv, "-f", &configFilePath) == EXIT_FAILURE)
    {
        syslog(LOG_ERR, "Invalid input params. Use -f path_to_config.");
        goto exit;
    }
    
    /*
     * read settings
     */
    if(readSettings(configFilePath) == EXIT_FAILURE)
    {
        syslog(LOG_ERR, "Configuration file error.");
        goto exit;
    }    

    if(isdaemon == 1)
    {
	 daemon(1, 0);	 
    }

    /* Create pid file */

    FILE* pidf = fopen(pidfile, "w");

    if(pidf==NULL)
    {
        syslog(LOG_ERR, "Failed to write pid file");
	return;
    }

    fprintf(pidf, "%d", getpid());

    fclose(pidf);    

    /* Set signal handlers */
	sigset_t sigset;
	sigemptyset(&sigset);
	struct sigaction siginfo = {
		.sa_handler = sighandler,
		.sa_mask = sigset,
		.sa_flags = SA_RESTART,
	};
	sigaction(SIGHUP, &siginfo, NULL);
	sigaction(SIGTERM, &siginfo, NULL);
    
    pthread_mutex_init(&connectionm, NULL);
	
    connections = (PGconn**)malloc(sizeof(PGconn*) * CONNECTION_BACKLOG);
    selectconnectionlock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * CONNECTION_BACKLOG);
    
    int i;
    for(i = 0; i < CONNECTION_BACKLOG; i++)
    {
        connections[i] = NULL;
        db_login(&connections[i]);
        pthread_mutex_init(&selectconnectionlock[i], NULL);
    }
    
    struct lws_context_creation_info info;
    struct lws_protocols protocol;
    struct lws_context *context;
    // Not using ssl
    const char *cert_path = NULL;
    const char *key_path = NULL;
    // no special options
    int opts = 0;

    //* setup websocket protocol */
    protocol.name = "websocket_protocol";
    protocol.callback = ws_service_callback;
    protocol.per_session_data_size=sizeof(struct per_session_data);
    protocol.rx_buffer_size = 0;

    //* setup websocket context info*/
    memset(&info, 0, sizeof info);
    info.port = port;
    info.iface = addr;
    info.protocols = &protocol;
    info.extensions = lws_get_internal_extensions();
    info.ssl_cert_filepath = cert_path;
    info.ssl_private_key_filepath = key_path;
    info.gid = -1;
    info.uid = -1;
    info.options = opts;

    //* create libwebsocket context. */
    context = lws_create_context(&info);
    if (context == NULL) {
        syslog(LOG_ERR, "Websocket context create error.");
        return -1;
    }

    syslog(LOG_INFO, "Websocket context create success.");

    //* websocket service */
    while ( !destroy_flag ) {
        lws_service(context, 1);
    }
    usleep(10);
    lws_context_destroy(context);
  
  exit:
    free(connections);
    free(selectconnectionlock);
    syslog(LOG_INFO, "Stoping.");
    config_destroy(&cfg);
    closelog();    
    return EXIT_SUCCESS;
}
