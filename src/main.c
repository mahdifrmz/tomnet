#include <stdio.h>
#include <toml.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include "ll.h"

#define MAX_HOST_NAME 256
#define MAX_HOST_NAME_STR "256"
#define MAX_IF_NAME 15
#define MAX_IF_NAME_STR "15"

typedef struct tn_intf_t tn_intf_t;
typedef struct tn_host_t tn_host_t;

struct tn_intf_t {
    char *name;
    int is_valid;
    tn_host_t *host;
    tn_intf_t *created_for;
    tn_intf_t *link;
    tn_intf_t *ll_prev;
    tn_intf_t *ll_next;
};

struct tn_host_t {
    char *name;
    int is_switch;
    int is_valid;
    tn_intf_t *created_for;
    tn_intf_t *intfs_ll;
    tn_host_t *ll_next;
    tn_host_t *ll_prev;
};

typedef struct {
    int pre_peered_count;
    int is_valid;
    tn_host_t *hosts_ll;
} tn_db_t;

typedef struct {
    tn_db_t db;
    tn_host_t *cur_host;
} tn_parser_t;

tn_host_t *tn_lookup_host(tn_db_t *db, const char *name) {
    size_t idx;
    tn_host_t *host;
    if(!name[0]) {
        return NULL;
    }
    ll_foreach(db->hosts_ll, host, idx) {
        if(!strcmp(name,host->name)) {
            return host;
        }
    }
    return NULL;
}

tn_intf_t *tn_lookup_intf(tn_host_t* host, const char *name) {
    size_t idx;
    tn_intf_t *intf;
    if(!name[0]) {
        return NULL;
    }
    ll_foreach(host->intfs_ll, intf, idx) {
        if(!strcmp(name,intf->name)) {
            return intf;
        }
    }
    return NULL;
}

tn_host_t *tn_add_host(tn_db_t *db, const char *name) {
    tn_host_t *host = NULL;
    tn_host_t *lookup = tn_lookup_host(db, name);
    if (!lookup || !lookup->created_for) {
        host = malloc(sizeof(tn_host_t));
        host->name = strdup(name);
        host->is_switch = 0;
        host->intfs_ll = NULL;
        host->created_for = NULL;
        host->is_valid = !lookup && name[0];
        ll_bpush(db->hosts_ll, host);
    } else {
        host = lookup;
        db->pre_peered_count--;
    }
    host->created_for = NULL;
    return host;
}

tn_intf_t *tn_add_intf(tn_db_t *db, tn_host_t* host, const char *name) {
    tn_intf_t *intf = NULL; 
    tn_intf_t *lookup = tn_lookup_intf(host, name);
    if (!lookup || !lookup->created_for) {
        intf = malloc(sizeof(tn_intf_t));
        intf->name = strdup(name);
        intf->host = host;
        intf->created_for = NULL;
        intf->is_valid = !lookup && name[0];
        intf->link = NULL;
        ll_bpush(host->intfs_ll, intf);
    } else {
        db->pre_peered_count--;
        intf = lookup;
    }
    intf->created_for = NULL;
    return intf;
}

void tn_db_link(tn_db_t *db, tn_intf_t *intf, const char *peer_name, const char *peer_intf_name) {
    tn_host_t *peer = tn_lookup_host(db, peer_name);
    if(!peer) {
        peer = malloc(sizeof(tn_host_t));
        peer->name = strdup(peer_name);
        peer->is_switch = 0;
        peer->intfs_ll = NULL;
        peer->created_for = intf;
        peer->is_valid = 1;
        ll_bpush(db->hosts_ll, peer);
        db->pre_peered_count++;
    }
    tn_intf_t *peer_intf = tn_lookup_intf(peer, peer_intf_name);
    if(!peer_intf) {
        peer_intf = malloc(sizeof(tn_intf_t));
        peer_intf->name = strdup(peer_intf_name);
        peer_intf->host = peer;
        peer_intf->link = intf;
        peer_intf->created_for = intf;
        peer_intf->is_valid = 1;
        ll_bpush(peer->intfs_ll, peer_intf);
        db->pre_peered_count++;
    }
    intf->link = peer_intf;
    peer_intf->link = intf;
}

void tn_parse_error(tn_parser_t *parser, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    parser->db.is_valid = 0;
}

void tn_parse_intf(tn_parser_t *parser, toml_table_t *tintf) {
    toml_datum_t name = toml_string_in(tintf, "name");
    if(!name.ok || !name.u.s[0]) {
        tn_parse_error(parser, "Interface must have a name");
    }
    tn_intf_t *intf = tn_add_intf(&parser->db, parser->cur_host, name.ok ? name.u.s : "");
    toml_datum_t link = toml_string_in(tintf, "peer");
    if(link.ok && link.u.s[0]) {
        toml_datum_t link_if = toml_string_in(tintf, "peer-if");
        if(!link_if.ok || !link_if.u.s[0]) {
            tn_parse_error(parser, "Peer interface must be specified");
        } else {
            if(intf->link) {
                if(strcmp(intf->link->host->name, link.u.s) || strcmp(intf->link->name, link_if.u.s)) {
                    tn_parse_error(parser, "Interface already linked to another interface");
                }
            } else {
                tn_db_link(&parser->db, intf, link.u.s, link_if.u.s);
            }
        }
    }
}

void tn_parse_host(tn_parser_t *parser, toml_table_t *thost) {
    toml_datum_t name = toml_string_in(thost, "name");
    if(!name.ok) {
        tn_parse_error(parser, "Host must have a name");
    }
    if(strlen(name.u.s) > MAX_HOST_NAME)
    {
        tn_parse_error(parser, "Host name maximum length is " MAX_HOST_NAME_STR);
    }
    tn_host_t *host = tn_add_host(&parser->db, name.u.s);
    if(!host->is_valid) {
        tn_parse_error(parser, "Host name must be unique");
    }
    parser->cur_host = host;
    toml_array_t *intfs = toml_array_in(thost, "if");
    if(intfs) {
        int intfs_count = toml_array_nelem(intfs);
        for(int i=0; i<intfs_count; i++) {
            toml_table_t *tintf = toml_table_at(intfs, i);
            tn_parse_intf(parser, tintf);
        }
    }
    parser->cur_host = NULL;
}

void tn_parse_root(tn_parser_t *parser, toml_table_t *root) {
    toml_array_t *hosts = toml_array_in(root, "host");
    if(!hosts)
        return;
    int host_count = toml_array_nelem(hosts);
    for(int i=0; i<host_count; i++) {
        toml_table_t *host = toml_table_at(hosts, i);
        tn_parse_host(parser, host);
    }
}

void tn_db_init(tn_db_t *db) {
    db->hosts_ll = NULL;
    db->is_valid = 1;
    db->pre_peered_count = 0;
}

void tn_db_destroy(tn_db_t *db) {
    tn_host_t *host;
    tn_intf_t *intf;
    while(db->hosts_ll) {
        ll_fpop(db->hosts_ll, host);
        while(host->intfs_ll) {
            ll_fpop(host->intfs_ll, intf);
            free(intf->name);
            free(intf);        
        }
        free(host->name);
        free(host);
    }
}

tn_db_t tn_parse(const char *file_path) {
    tn_parser_t parser;
    parser.cur_host = NULL;
    tn_db_init(&parser.db);
    char errbuf[256];
    memset(errbuf, 0, sizeof(errbuf));
    FILE *file = fopen(file_path, "rb");
    if(!file) {
        tn_parse_error(&parser, "failed to open file: %s\n", file_path);
    } else {
        toml_table_t *root = toml_parse_file(file, errbuf, sizeof(errbuf));
        if(!root) {
            tn_parse_error(&parser, "incorrect TOML format in file: %s\n%s\n", file_path, errbuf);
        }
        tn_parse_root(&parser, root);
        if(parser.db.pre_peered_count) {
            tn_host_t *host;
            tn_intf_t *intf;
            size_t _i, _j;
            ll_foreach(parser.db.hosts_ll, host, _j) {
                if(host->created_for) {
                    tn_parse_error(&parser, "Host %s refered to as peer, but not created explicitly\n", host->name);
                }
                ll_foreach(host->intfs_ll, intf, _i) {
                    tn_parse_error(&parser, "Interface %s/%s refered to as peer interface, but not created explicitly\n", host->name, intf->name);
                }    
            }
            parser.db.is_valid = 0;
        }
        if(!parser.db.is_valid) {
            tn_db_destroy(&parser.db);
        }
    }
    return parser.db;
}

int main(int argc, char **argv)
{
    tn_db_t db = tn_parse(argv[1]);
    tn_host_t *host;
    tn_intf_t *intf;
    size_t _i, _j;
    ll_foreach(db.hosts_ll, host, _j) {
        printf("host: %s\n", host->name);
        ll_foreach(host->intfs_ll, intf, _i) {
            printf("if: %s\n", intf->name);
            if(intf->link)
            printf("\t-> %s.%s\n", intf->link->host->name, intf->link->name);
        }
    }
            
    return !db.is_valid;
}