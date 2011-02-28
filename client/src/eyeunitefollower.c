#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "bootstrap.h"
#include "followernode.h"
#include "tree.h"
#include "debug.h"

// Global variables for threads
struct peer_info upstream_peer;
void* upstream_sock;
struct peer_node* downstream_peers;
size_t num_downstream_peers;
pthread_mutex_t downstream_peers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t upstream_peer_mutex = PTHREAD_MUTEX_INITIALIZER;
const char* endpoint = "tcp://*:55555";
char my_pid[EU_TOKENSTRLEN];
char my_addr[EU_ADDRSTRLEN];
int my_bw;
int my_port;

struct peer_node
{
  struct peer_info peer_info;
  struct peer_node* next;
};

struct peer_node* peer_node(struct peer_info node_params)
{
  struct peer_node* pn = (struct peer_node*)malloc(sizeof(struct peer_node));
  pn->peer_info = node_params;
  pn->next = NULL;
  return pn;
};

void add_downstream_peer(struct peer_node* new_peer)
{
  if(downstream_peers == NULL)
    downstream_peers = new_peer;
  else
  {
    struct peer_node* currNode = downstream_peers;
    while(currNode->next != NULL)
      currNode = currNode->next;
    currNode->next = new_peer;
  }
  return;
}

void drop_downstream_peer(struct peer_node* new_peer)
{
  if(downstream_peers == NULL)
    return;
  else
  {
    struct peer_node* currNode = downstream_peers;
    while(currNode->next != NULL)
    {
      if(currNode->next->peer_info.pid == new_peer->peer_info.pid)
      {
        struct peer_node* targetNode = currNode->next;
        currNode->next = currNode->next->next;
        free(targetNode);
        return;
      }
      currNode = currNode->next;
    }
  }
  return;
}

void change_upstream_peer(struct peer_info up_peer)
{
  upstream_peer = up_peer;
  char temp[EU_ADDRSTRLEN*4];
  snprintf (temp, EU_ADDRSTRLEN*4, "tcp://%s:%u", up_peer.addr, up_peer.port);
  upstream_sock = fn_initzmq (my_pid, temp);
}

void* statusThread(void* arg)
{
  while(1)
  {
  message_struct* msg = fn_rcvmsg(upstream_sock);
  if(!msg)
    print_error("Error: Status thread received null msg\n");
  print_error("Received Message: ");
  if(msg->type == FEED_NODE)
  {
    struct peer_node* pn = peer_node(msg->node_params);
    print_error("FEED_NODE\n");
    print_error("Adding downstream peer %s\n", msg->node_params.pid);
    pthread_mutex_lock(&downstream_peers_mutex);
    add_downstream_peer(pn);
    pthread_mutex_unlock(&downstream_peers_mutex);
  }
  else if(msg->type == DROP_NODE)
  {
    struct peer_node* pn = peer_node(msg->node_params);
    print_error("DROP_NODE\n");
    print_error("Dropping downstream peer %s\n", msg->node_params.pid);
    pthread_mutex_lock(&downstream_peers_mutex);
    drop_downstream_peer(pn);
    pthread_mutex_unlock(&downstream_peers_mutex);
  }
  else if(msg->type == FOLLOW_NODE)
  {
    struct peer_info pi = msg->node_params;
    print_error("FOLLOW_NODE\n");
    print_error("Changing upstream peer %s\n", pi.pid);
    pthread_mutex_lock(&upstream_peer_mutex);
    change_upstream_peer(pi);
    pthread_mutex_unlock(&upstream_peer_mutex);
  }
  else
  {
  }
  }
}

int main(int argc, char* argv[])
{
  int i;
  struct bootstrap* b;
  struct peer_info source_info;
  char* lobby_token;
  void* sock;

  // Follower peer info variables
  struct peer_info* my_peer_info;

  if(argc < 4)
  {
    printf("Usage:\n");
    printf("eyeunitefollower <lobby token> <listen port> <bandwidth>");
    return -1;
  }
  lobby_token = argv[1];
  my_port = atoi(argv[2]);
  my_bw = atoi(argv[3]);

  // Bootstrap
  if(!(b = bootstrap_init(APP_ENGINE, 8080, my_pid, my_addr)))
  {
    print_error("Failed intitializing bootstrap!\n");
    return 1;
  }
  if(bootstrap_lobby_join(b, lobby_token))
  {
    print_error("Failed joining lobby %s\n", lobby_token);
    return 1;
  }
  if(bootstrap_lobby_get_source(b, &source_info))
  {
    print_error("Failed to get source\n");
    return 1;
  }

  print_error ("aok");

  // Set my peer_info
  my_peer_info = malloc (sizeof *my_peer_info);
  memcpy(my_peer_info->pid, my_pid, EU_TOKENSTRLEN);
  memcpy(my_peer_info->addr, my_addr, EU_ADDRSTRLEN);
  my_peer_info->port = my_port;
  my_peer_info->peerbw = my_bw;
  print_error ("aok");

  // Finish initialization
  downstream_peers = NULL;
  num_downstream_peers = 0;
  print_error ("aok");

  // Initiate connection to source
  print_error ("source pid: %s", source_info.pid);
  print_error ("source ip: %s", source_info.addr);
  char temp[EU_ADDRSTRLEN*4];
  snprintf (temp, EU_ADDRSTRLEN*4, "tcp://%s:%u", source_info.addr, source_info.port);
  print_error ("tmp addr: %s", temp);
  upstream_sock = fn_initzmq (my_pid, temp);
  fn_sendmsg(upstream_sock, REQ_JOIN, my_peer_info);
  print_error ("aok");

  pthread_t status_thread;

  // NOT USED YET
  // pthread_t data_rcv_thread;
  // pthread_t data_push_thread;
  // No display thread yet

  // Start status thread
  pthread_create(&status_thread, NULL, statusThread, NULL);

  pthread_join(status_thread, NULL);

  fn_closesocket(upstream_sock);

  return 0;
}
