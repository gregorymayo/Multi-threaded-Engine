//CS 149 - prodcon
//Xiao Han, explain about the function that I should pass on the pthread_create parameter
//Andres Cortes, helped me to find some mistakes about my code, especially in the struct part, and pthread_join
//Maneek, helped me to find my mistake about the run_producer_f and run_consumer_f in my main
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <memory.h>
#include "prodcon.h"
#include <pthread.h>

struct llist_node {
	struct llist_node *next;
	char *str;
};

struct producer_arg{
	int num;
	int producerCount;
	produce_f produce;
	int argc;
	char** argv;
	run_producer_f runProducer;
};

struct consumer_arg{
	int num;
	consume_f consume;
	int argc;
	char** argv;
	run_consumer_f runConsumer;
};

//static pthread_mutex_t **lock;
//static pthread_cond_t **cond;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/**
 * pop a node off the start of the list.
 *
 * @param phead the head of the list. this will be modified by the call unless the list is empty
 * (*phead == NULL).
 * @return NULL if list is empty or a pointer to the string at the top of the list. the caller is
 * incharge of calling free() on the pointer when finished with the string.
 */
char *pop(struct llist_node **phead)
{
    pthread_mutex_lock(&lock);
    while (*phead == NULL) {
        pthread_cond_wait(&cond,&lock);
    }
    char *s = (*phead)->str;
    struct llist_node *next = (*phead)->next;
    free(*phead);
    *phead = next;
    pthread_mutex_unlock(&lock);
    return s;
}

/**
 * push a node onto the start of the list. a copy of the string will be made.
 * @param phead the head of the list. this will be modified by this call to point to the new node
 * being added for the string.
 * @param s the string to add. a copy of the string will be made and placed at the beginning of
 * the list.
 */
void push(struct llist_node **phead, const char *s)
{
    pthread_mutex_lock(&lock);
    while (*phead == NULL) {
        pthread_cond_wait(&cond,&lock);
    }
    struct llist_node *new_node = malloc(sizeof(*new_node));
    new_node->next = *phead;
    new_node->str = strdup(s);
    *phead = new_node;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

// the array of list heads. the size should be equal to the number of consumers
static struct llist_node **heads;
static assign_consumer_f assign_consumer;
static int producer_count;
static int consumer_count;
static int my_consumer_number;

void queue (int consumer, const char *str){
    push(&heads[consumer], str);
}

void produce(const char *buffer)
{
    pthread_mutex_lock(&lock);
    int hash = assign_consumer(consumer_count, buffer);
    queue(hash, buffer);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

char *consume() {
    pthread_mutex_lock(&lock);
    char *str = pop(&heads[my_consumer_number]);
    pthread_mutex_unlock(&lock);
    return str;
}

void *start_thread_producer(void *arg){
    pthread_mutex_lock(&lock);
    struct producer_arg *pro = (struct producer_arg *) arg;
    run_producer_f run_producer = pro->runProducer;
    pthread_cond_signal(&cond);
    run_producer(pro->num, pro->producerCount, pro->produce, pro->argc, pro->argv);
    pthread_mutex_unlock(&lock);
}

void *start_thread_consumer(void *arg){
    pthread_mutex_lock(&lock);
    struct consumer_arg *con = (struct consumer_arg *) arg;
    run_consumer_f run_consumer = con->runConsumer;
    pthread_cond_signal(&cond);
    run_consumer(con->num, consume, con->argc, con->argv);
    pthread_mutex_unlock(&lock);
}

void do_usage(char *prog)
{
    printf("USAGE: %s shared_lib consumer_count producer_count ....\n", prog);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        do_usage(argv[0]);
    }
	
    char *shared_lib = argv[1];
    producer_count = strtol(argv[2], NULL, 10);
    consumer_count = strtol(argv[3], NULL, 10);

    char **new_argv = &argv[4];
    int new_argc = argc - 4;
    setlinebuf(stdout);

    if (consumer_count <= 0 || producer_count <= 0) {
        do_usage(argv[0]);
    }

    void *dh = dlopen(shared_lib, RTLD_LAZY);

    // load the producer, consumer, and assignment functions from the library
    run_producer_f run_producer = dlsym(dh, "run_producer");
    run_consumer_f run_consumer = dlsym(dh, "run_consumer");
    assign_consumer = dlsym(dh, "assign_consumer");
    if (run_producer == NULL || run_consumer == NULL || assign_consumer == NULL) {
        printf("Error loading functions: prod %p cons %p assign %p\n", run_producer,
                run_consumer, assign_consumer);
        exit(2);
    }

    heads = calloc(consumer_count, sizeof(*heads));

    pthread_t producers[producer_count];
    pthread_t consumers[consumer_count];

    
    for (int i = 0; i < producer_count; i++) {
        struct producer_arg structProducer;
        structProducer.num = i;
        structProducer.producerCount = producer_count;
        structProducer.argc = new_argc;
        structProducer.argv = new_argv;
        structProducer.produce = produce;
        structProducer.runProducer = run_producer;
        pthread_create(&producers[i],NULL,start_thread_producer,&structProducer);
        pthread_cond_wait(&cond,&lock);
    }
    
    for (int i = 0; i < consumer_count; i++) {
        struct consumer_arg structConsumer;
        structConsumer.num = i;
        structConsumer.argc = new_argc;
        structConsumer.argv = new_argv;
        structConsumer.consume = consume;
        structConsumer.runConsumer = run_consumer;
        pthread_create(&consumers[i],NULL,start_thread_consumer,&structConsumer);
        pthread_cond_wait(&cond,&lock);
    }

    //pthread join and wait for producers
    for(int i =0; i<producer_count;i++){
       pthread_join(producers[i],NULL);
    }
    //pthread join and wait for consumer
    for(int i=0;i<consumer_count;i++){
        pthread_join(consumer[i],NULL);
    }
    return 0;
}
