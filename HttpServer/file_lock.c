#include "file_lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct node node;
struct node{
    rwlock_t* rwl;
    char* fileName;
    int ref_count;
    node* next;
};
    
struct file_lock{
    node* head;
    node* tail;
    int size;
};

file_lock* create_file_lock(void){
    file_lock* fl = malloc(sizeof(file_lock));
    if(!fl){
        return NULL;
    }
    fl -> head = NULL;
    fl -> tail = NULL;
    fl -> size = 0;
    return fl;
}
void delete_file_lock(file_lock** fl){
    if(fl == NULL || *fl == NULL){
        return;
    }

    while((*fl) -> head != NULL){
        node* old = (*fl) -> head;
        rwlock_delete(&(old -> rwl));
        free(old->fileName);
        (*fl)->head = (*fl)->head->next;
        free(old);
    }
    (*fl)->tail = NULL;
    (*fl)->size = 0;

    free(*fl);
    *fl = NULL;
}
bool insert_to_file_lock(file_lock* fl, char* fileName, rwlock_t* rwl){
    if(!fl){
        return false;
    }
    
    node* new_node = malloc(sizeof(node));
    if(!new_node){
        return false;
    }
    new_node -> rwl = rwl; 
    new_node->fileName = strdup(fileName);
    if (!new_node->fileName) {
        free(new_node);
        return false;
    }
    new_node->ref_count = 1;
    new_node -> next = NULL;

    if(fl -> size == 0){
        fl -> head = new_node;
        fl -> tail = new_node;
    }else{
        fl -> tail-> next = new_node;
        fl -> tail = new_node;
    }

    fl -> size += 1;
    return true;
}
bool remove_from_file_lock(file_lock* fl, char* fileName){
    if(!fl || fl -> size == 0){
        return false;
    }

    node* current = fl -> head;
    node* prev = NULL;
    
    while(current != NULL){
        if (strcmp(current->fileName, fileName) == 0) {
            current -> ref_count -= 1;
            if(current -> ref_count > 0){
                return true;
            }
            if (current == fl->head) {
                fl->head = current->next; 
                if (fl->head == NULL) {
                    fl->tail = NULL; 
                }
            } else {
                prev->next = current->next;
                if (current == fl->tail) {
                    fl->tail = prev;
                }
            }

            rwlock_delete(&(current->rwl));  
            free(current->fileName);   
            free(current);                 
            fl->size -= 1;                 
            return true;
        }
        prev = current; 
        current = current->next;
    }
    return false;
}
bool look_for_item(file_lock* fl, char* fileName, rwlock_t** rwl){
    if(!fl || !rwl ||fl -> size == 0){
        return false;
    }

    node* current = fl -> head;
    while(current != NULL){
        if(strcmp(current -> fileName, fileName) == 0){
            *rwl = current -> rwl;
            current->ref_count++;
            return true;
        }
        current = current -> next;
    }
    return false;
}
