#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include "../common/mot_maze.h"

#define MOTSRV_ADDR     "127.0.0.1"
#define MOTSRV_PORT     "6666"
#define BACKLOG         8

#define MAZE_MAGIC      0x6D7A
#define ADD_PLAYER      0x4E45
#define HUNTER          0x4855
#define ILLEGAL_MOV     0xF000
#define PLAYER_MOV      0x4D4F
#define PLAYER_DC       0x4443
#define PLAYER_DIE      0x4B4F
#define PLAYER_WIN      0x5749
#define SRV_BUSY        0xEEEE

int mrand(int floor, int ceil);

void *
get_in_addr(struct sockaddr *sa)
{
   if (sa->sa_family == AF_INET)
   {
      return &(((struct sockaddr_in*)sa)->sin_addr);
   }

   return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void
sendall(int s, char *buf, size_t len)
{
   int total = 0;          // how many bytes we've sent
   int bytesleft = len;    // how many we have left to send
   int n;

   while(total < len)
   {
      n = send(s, buf+total, bytesleft, 0);

      if (n == -1)
      {
         perror("sendall");
         exit(EXIT_FAILURE);
      }

      total += n;
      bytesleft -= n;
   }
}


int
main(int argc, char *argv[])
{
   int ssockfd, csockfd, err;;
   unsigned short magic;
   unsigned int u;
   struct addrinfo hints, *srvinfo, *p;
   struct sockaddr_storage caddr;
   socklen_t addr_size;
   size_t len, bytes_sent;

   int i, j;

   /* For handling multiple clients using select() */

   fd_set master;
   fd_set read_fds;  /* File descriptors with readable data */
   int fdmax;        /* Highest file descriptor in the master set */
   int newfd;        /* File descriptor for handling incoming connections */
   char buf[256];
   int nbytes;
   char remoteIP[INET6_ADDRSTRLEN];

   FD_ZERO(&master);    /* Empty the master set */
   FD_ZERO(&read_fds);  /* Empty the readfds set */

   genmaze(20, 20);
   memset(&hints, 0, sizeof(hints));

   /* Currently using TCP. */
   hints.ai_family   = AF_UNSPEC;
   hints.ai_flags    = AI_PASSIVE;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;
   
   if (err = getaddrinfo(MOTSRV_ADDR, MOTSRV_PORT, &hints, &srvinfo))
   {
      fprintf(stderr, "Failed to get address: ", gai_strerror(err));
      return 10;
   }

   /*
    * Allocate socket file descriptor with address information acquired
    * from getaddrinfo.
    */

   if (ssockfd == -1)
   {
      perror("Failed to allocate server socket descriptor.");
      return 7;
   }

   for (p = srvinfo; p != NULL; p = p->ai_next)
   {
      ssockfd = socket( p->ai_family, p->ai_socktype, p->ai_protocol);

      if( ssockfd < 0 )
      {
         continue;
      }

      /*
       * If system thinks the socket is on use but it isn't, fix it...
       */
      setsockopt( ssockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(int) );

      /*
       * Bind socket to port.
       */
      if( bind( ssockfd, p->ai_addr, p->ai_addrlen) < 0 )
      {
         close(ssockfd);
         continue;
      }

      break;

   }


   /*
    * If p reached NULL, it means we didn't bind at all.
    */

   if (p == NULL)
   {
      fprintf( stderr, "failed to bind ssockfd\n" );
      return 2;
   }

   freeaddrinfo(srvinfo);

   /*
    * Start listening on server socket descriptor.
    */

   if (listen(ssockfd, BACKLOG ) == -1)
   {
      perror("Server can't listen.");
      return 3;
   }

   /*
    * Add ssockfd to master set of sockets.
    */
   FD_SET(ssockfd, &master);

   /*
    * Since ssockfd is currently the only socket in master,
    * it's the highest descriptor.
    */
   fdmax = ssockfd;


   while (1)
   {
      read_fds = master;

      if (select(fdmax + 1, &read_fds, NULL, NULL, NULL ) == -1)
      {
         perror("select");
         return 4;
      }

      /*
       * Now read_fds has only those sockets that have readable data to
       * show.
       * Run through all the sockets to find data to read.
       */

      for (i = 0; i <= fdmax; i++)
      {
         if (!FD_ISSET(i, &read_fds))
         {
            continue;
         }

         /* If we're here, i has data to show */
         if (i == ssockfd)
         {
            /* Handle new connections. */

            addr_size = sizeof caddr;
            newfd = accept(ssockfd,
                  (struct sockaddr *) &caddr, &addr_size);

            if (newfd == -1)
            {
               perror("accept");
            }
            else
            {
               /* Add the new socket descriptor to master. */
               FD_SET(newfd, &master);

               if (newfd > fdmax) 
               {
                  newfd = fdmax;
               }
               printf("selectserver: new connection from %s on socket %d\n",
                        inet_ntop(caddr.ss_family,
                        get_in_addr((struct sockaddr*)&caddr),
                        remoteIP, INET6_ADDRSTRLEN), newfd );

               /*
                * When a player first connects, send maze magic, data
                * width, size. Then send the maze itself.  Then await
                * confirmation.
                */

               magic = htons(MAZE_MAGIC);
               
               sendall( newfd, (char * ) &magic, sizeof( magic ) );

               u = htonl( MAZE.w );

               sendall( newfd, (char *) &u, sizeof( u ) );

               u = htonl( MAZE.size );
               sendall( newfd, (char *) &u, sizeof( u ) );

               sendall( newfd, MAZE.data, MAZE.size );

               char PLAYANAME[32];

               recv( newfd, &PLAYANAME, 32, 0 );

               printf("%s connected !!!\n", PLAYANAME );
            }
         }
         else
         {
            /* Handle data from a client. */

            if ((nbytes = recv(i, buf, sizeof buf, 0 )) <= 0)
            {
               if (nbytes == 0)
               {
                  /* Client closed connection. */
                  printf("selectserver: socket %d hung up\n", i);
               }
               else
               {
                  perror("recv");
               }
               close(i);
               FD_CLR(i, &master);
            }
            else
            {
               /* we got some data to read,son */

               for (j = 0; j <= fdmax; j++)
               {
                  if (FD_ISSET(j, &master))
                  {
                     /*
                      * don't send it to server and the client
                      * who sent the data
                      */

                     if (j != ssockfd && j != i)
                     {
                        if (send(j, buf, nbytes, 0) == -1)
                        {
                           perror("send");
                        }
                     }
                  }
               }
            }
         }
      }
   }

            
   /*
    * Free things (sockets, addrinfo, player data, maze) and exit.
    */
   close(ssockfd);
   close(csockfd);
   freeaddrinfo(srvinfo);
   free(MAZE.data);

   return 0;
}
