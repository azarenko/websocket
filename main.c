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
#include <libwebsockets.h>

#include "cmdparam.h"
#include "settings.h"
#include "sockutils.h"
#include "proto.h"
#include "fifo.h"

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
            break;
    }
}
 
void websocket_receive(struct lws *wsi_in, char *content, int str_size_in) 
{
    if(isfifofull())
    {
      
    }
    else
    {
      wsinf *pwsinf;
      pwsinf = malloc(sizeof(wsinf));
      pwsinf->wsi_in = wsi_in;
      pwsinf->content = content;
      pwsinf->str_size_in = str_size_in;
      fifo_put(pwsinf);
    }
    
    return;
}


static int ws_service_callback(
                         struct lws *wsi,
                         enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{
    switch (reason) {

        case LWS_CALLBACK_RECEIVE:
            websocket_receive(wsi ,(char *)in, -1);
            break;
	
	case LWS_CALLBACK_SERVER_WRITEABLE:
	    return -1;
	    
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
	
    fifo_init();
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

    for(i=0; i<CONNECTION_BACKLOG; i++)
    {
        pthread_t pth;	
        pthread_create(&pth, NULL, threadFunc, NULL);
    }
    
    //* websocket service */
    while ( !destroy_flag ) {
        lws_service(context, 1);
    }
    usleep(10);
    lws_context_destroy(context);
  
  exit:
    fifo_free();
    free(connections);
    free(selectconnectionlock);
    syslog(LOG_INFO, "Stoping.");
    config_destroy(&cfg);
    closelog();    
    return EXIT_SUCCESS;
}
