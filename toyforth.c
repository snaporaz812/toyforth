#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

// ============== DATA STRUCTURES ==============

#define TFOBJ_TYPE_INT 0
#define TFOBJ_TYPE_STRING 1
#define TFOBJ_TYPE_BOOL 2
#define TFOBJ_TYPE_LIST 3
#define TFOBJ_TYPE_SYMBOL 4


typedef struct tfobj{
	int refcount;
	int type; // TFOBJ_TYPE_*

	// Position in the file
	int line, line_offset;

	union {
		// INT and BOOL
		int i; // FIXME: ma non è che dovrei farlo signed int forse???

		// STRING and SYMBOL
		struct {
			char *ptr;
			size_t len;
		} str;

		// LIST
		struct { 
			struct tfobj **ele; //list.ele is the array that contains list.ele[0], list.ele[1], etc
			size_t len;
		} list;
	};
} tfobj;

typedef struct tfparser {
	int line, line_offset;	
	char *prg;	// <-- The program to compile into a list
	char *p;	// <-- Next token to parse
} tfparser;

// Usage context
typedef struct tfctx{ 
	tfobj *stack; // It is goint to be used as a LIST
} tfctx;


// ============== ALLOCATION WRAPPERS ==============

// Try to allocate n bytes, else close the program
void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL) {
		fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
}

// ============== OBJECT-RELATED FUNCTIONS ==============

/*Allocate and initialize ToyForth Objects of the required types*/

tfobj *create_object(int type){
	tfobj *o = xmalloc(sizeof(tfobj));
	o->type = type;
	o->refcount = 1;
	/*
	o->line = 0;
	o->line_offset = 0;
	*/
	return o;
};


void destroy_tfobject(tfobj *o) {
	if (o->refcount > 1) {
		fprintf(stderr, "Object still has references\n");
		exit(1);

	} else if (o->refcount < 1) {
		fprintf(stderr, "An anomaly with the references of the object occurred");
		exit(1);

	} else free(o); // refcount == 0 --> tfobj gets destroyed
};

tfobj *create_text_object(char *s, size_t len, int type) {
	tfobj *o = create_object(type);
	o->str.ptr = xmalloc(len+1);
	memcpy(o->str.ptr, s, len);
	o->str.ptr[len] = 0;
	o->str.len = len;

	return o;	
};

tfobj *create_string_object(char *s, size_t len) {
	tfobj *o = create_text_object(s, len, TFOBJ_TYPE_STRING);
	return o;
};

tfobj *create_int_object(int i) {
	tfobj *o = create_object(TFOBJ_TYPE_INT);
	o->i = i;
	return o;
};

tfobj *create_bool_object(int i){
	tfobj *o = create_object(TFOBJ_TYPE_BOOL);
	o->i = i;
	return o;
};


tfobj *create_symbol_object(char *s, size_t len) {
	tfobj *o = create_text_object(s, len, TFOBJ_TYPE_SYMBOL);
	return o;
};


tfparser *create_tfparser(char *prgtext) {
	tfparser *o = xmalloc(sizeof(tfparser));
	o->prg = prgtext; // prgtext is set as current string
	o->p = prgtext; // prgtext is set as next string to be read
	o->line = 1;
	o->line_offset = 1;
	return o;
};

// Increase reference count of the specified Toyforth object
void retain(tfobj *o) {
	o->refcount++;
};

// Decrease reference count of the specified Toyforth object
void release(tfobj *o) {
	o->refcount--;
	if (o->refcount == 0) destroy_tfobject(o);
};

// ================= LIST OBJECT FUNCTIONS =======================

tfobj *create_list_object(void) {
	tfobj *o = create_object(TFOBJ_TYPE_LIST);
	o->list.ele = NULL;
	o->list.len = 0;
	return o;
};


/* Append a new element to the end of a list,
 * The caller should then increase the reference count manually.
*/
void append(tfobj *l, tfobj *ele) {
	// Reallocate the memory so that it can contain the elements of the array 
	tfobj **new_ele = realloc(l->list.ele, sizeof(tfobj *) * (l->list.len + 1));
    if (!new_ele) {
        // Handle memory allocation failure appropriately
		fprintf(stderr, "Error appending an element to a list");
        goto safelander; 
    }

    l->list.ele = new_ele;

	// Append the element at the last position of the list
	l->list.ele[l->list.len] = ele;

	// Increase refcount and list length
	retain(ele);
	l->list.len++;

	safelander:
};

// ============= EXECUTION CONTEXT ================

tfctx *create_context() {
	tfctx *ctx = xmalloc(sizeof(*ctx));
	ctx->stack = create_list_object();
	return ctx;
};

// ============= COMPILING AND PARSING =============

// Increase parser's p (aka the next token) and line_offset
void incr_parser_token(tfparser *parser) {
	parser->p++;
	parser->line_offset++;
}
 // Skip whitespaces and comments (#...\n)
void skip_trivia(tfparser *parser) {
	while (1) {
		// Skip whitespaces
		while (parser->p[0] && isspace((unsigned char)parser->p[0])) {
			// Manage newlines correctly
			if (parser->p[0] == '\n') {
				parser->p++;
				parser->line_offset = 1;
				parser->line++;
			} else incr_parser_token(parser);
		}

		if (parser->p[0] == '#') {
			while (parser->p[0] && parser->p[0] != '\n') {incr_parser_token(parser);}
			continue;
		}

		break;
	}
};


#define MAX_NUM_LEN 128
// This function accepts a parser and returns a ToyForth int object containing the parsed integer numbers
tfobj *parse_numbers(tfparser *parser) {
	char buf[MAX_NUM_LEN];
	char *start = parser->p; // Record the start of the number (either number or sign)
	char *end;
	tfobj *o = NULL;

	/* This check should be useless IN THEORY, as it is already evaluated in compile()
	 * before parse_numbers() is called
	 */

	// Check whether the sign is followed by a number
	if (((parser->p[0] == '+') || (parser->p[0] == '-')) && isdigit(parser->p[1])) {incr_parser_token(parser);}
	else if (!isdigit(parser->p[0])) {return o;} // Return NULL object which will throw a segmentation fault.

	// Get to the end of the number and record its index
	while (parser->p[0] && isdigit(parser->p[0])) {incr_parser_token(parser);}
	end = parser->p;
	int numlen = end-start;

	// Error if number is too big
	if (numlen >= MAX_NUM_LEN) {return o;}

	// Copy parsed content to buffer, then set null term at the end of the array 
	memcpy(buf, start, numlen);
	buf[numlen] = 0;

	/* Create a tfobject containing the parsed number as an integer.
	 *	atoi() "casts" an ASCII string to int 
	 */
	o = create_int_object(atoi(buf));
	o->line_offset = parser->line_offset;

	return o; 
};

#define MAX_STRING_LEN 512 
#define STRING_CHAR 34 // ASCII 34 = "
tfobj *parse_strings(tfparser *parser) {

	char buf[MAX_STRING_LEN];
	char *start = parser->p;
	char *end;
	size_t len;
	tfobj *o = NULL;
	// define the charachter that will be checked for as the start of a string: " 
	char string_marker = (char)STRING_CHAR;

	// Check for: " 
	if (*start == string_marker) {
		//printf("AAAAAA *start: %c, Current token: %c\n", *start, parser->p[0]); //debug
		incr_parser_token(parser);

		while (parser->p[0] != string_marker) {
			//printf("BBBBBB *start: %c, Current token: %c\n", *start, parser->p[0]); //debug
			end = parser->p;
			incr_parser_token(parser);
		}
		incr_parser_token(parser); // Skip closing quotation mark 
	} else return o;
	
	len = end-start; // Don't count opening quotation mark // Hello World = 11
	if (len >= MAX_STRING_LEN) return o; // String is too big

	// Allocate the needed space (= string+'\0')
	memcpy(buf, start+1, len+1); // Cut off opening quotation mark
	buf[len] = 0;

	//printf("CCCCCC *end: %c, buf: %s, len: %zu\n", *end, buf, len); //debug

	o = create_string_object(buf, len);
	return o;
};


#define MAX_KEYWORD_LEN 5 // Symbols can be max 5 characters long
int issymbol(char *word) {

	// --- INITIALIZE KEYWORDS AND SYMBOLS ---
	const char *keywords[] = {
	// len = 3
	"dup",		// duplicate
	"add",		// add
	"sub",		// subtract
	"mul",		// multiply
	"div",		// divide
	"mod",		// modulo
	"var",		// variable
	"del",		// delete

	// len = 4
	"func",		// function

	// len = 5
	"print",	// print
	"reset",		// reset
	NULL
	};

	const char symbols[] = {"+*-/()[]{}"};
	// ---------------------------------------

	// Check punctuation
	size_t i = 0;
	while (symbols[i]) {
		if (strchr(word, (int)symbols[i])) {
			//printf("*** Punctuation: %s. issymbol = true, i=%zu\n", word,i); //debug
			return 1;
		}
			i++;
	}

	// Check keyword 
	i = 0;
	while (keywords[i]) {
		if (strcmp(word, keywords[i]) == 0) {
			//printf("*** Keyword: %s. issymbol = true, i=%zu\n", word,i); //debug
			return 1;
		}
		i++;
	}

	/*
	return 0 // false
	return 1 // true
	*/
	//printf("*** isymbol = false\n"); //debug
	return 0;
};


tfobj *parse_symbols(tfparser *parser) {
	
	char buf[MAX_KEYWORD_LEN];
	char *start = parser->p;
	char *end;
	size_t len;
	tfobj *o = NULL;


	// Extract the string to be evaluated
	while (isalpha(parser->p[0]) || ispunct(parser->p[0])) {
		end = parser->p;
		incr_parser_token(parser);
	}

	len = end-start+1;
	if (len > MAX_KEYWORD_LEN) return o; // Keyword is too big 

	memcpy(buf, start, len+1); // allocate space for string + null term
	buf[len] = 0;

	//printf("SSSSSS buf: %s, len: %zu\n", buf, len); // debug

	// Check if string is a SYMBOL
	if (issymbol(buf)) {
		//printf("parse_symbols buf: %s\n", buf); //debug
		o = create_symbol_object(buf, len);
	}
	return o;
};

// Translate .txt content into tf objects 
tfobj *compile(char *prg) {
	tfparser *parser = create_tfparser(prg);
	tfobj *parsed = create_list_object();

	// This cycle goes on as long as there are elements in the array parser->p[]
	while (parser->p) {
		tfobj *o = NULL;
		char *parse_token_start = parser->p;
		
		skip_trivia(parser);

		if (parser->p[0] == 0) break; // End of Program reached

		// Parse strings if quotation mark (") is found
		if (parser->p[0] == (char)STRING_CHAR) o = parse_strings(parser);

		// Parse numbers (either signed or unsigned)
		if (isdigit(parser->p[0]) ||
			((parser->p[0] == '+' || parser->p[0] == '-') && isdigit(parser->p[1]))) o = parse_numbers(parser);

		// Parse booleans

		// Parse symbols
		if (isalpha(parser->p[0]) || ispunct(parser->p[0])) o = parse_symbols(parser); 
		
		
		// Check for errors when token was being parsed
		if (o == NULL) {
			printf("\t---!!!---\n");
			fprintf(stderr, "\tSyntax error at line %d, col %d: \n|%8s|\n", parser->line, parser->line_offset, parse_token_start);
			printf("\t---!!!---\n");
			//return NULL;
			exit(1);
		} else {
			append(parsed, o);
		}
		
	}
	free(parser);
	return parsed;
};


void exec(tfobj *prg) {
	//Initialize stack instance
	tfctx *ctx = create_context(); //

	while (ctx->stack->list.ele) {
		switch (prg->type) {

		// --- Object must be appended to latest position in stack
		case TFOBJ_TYPE_BOOL:
		case TFOBJ_TYPE_INT:
		case TFOBJ_TYPE_SYMBOL:
		case TFOBJ_TYPE_STRING:
			append(ctx->stack, prg);
			break;

		case TFOBJ_TYPE_LIST:
			// manage functions (which are lists of operations)
			for (size_t i=0; i < prg->list.len; i++) exec(prg->list.ele[i]); // recursive
			break;

		default:
			fprintf(stderr, "Runtime error");
			exit(1);
		}
	}
};

void print_prg(tfobj *prg) {
	printf("[");

	for (size_t i=0; i < prg->list.len; i++) {
		tfobj *ele = prg->list.ele[i]; 
		switch (ele->type) {
		case TFOBJ_TYPE_INT:
		case TFOBJ_TYPE_BOOL:
			printf("%d", ele->i);
			break;

		case TFOBJ_TYPE_STRING:
			printf("\"%s\"", ele->str.ptr);
			break;

		case TFOBJ_TYPE_SYMBOL:
			printf("%s", ele->str.ptr); 
			break;

		case TFOBJ_TYPE_LIST:
			print_prg(ele); // Recursive call
		}
		if (i != prg->list.len -1) printf(" ");
	}
	printf("]\n");
};


// ============== MAIN ==============

int main(int argc, char **argv) {

	// ========== READ FROM FILE ==============
	if (argc < 2) {fprintf(stderr, "Usage: %s <filename>\n", *argv); return 1;}

	// Read the program in memory, for later parsing
	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading toyforth program\n");
		return 1;
	}

	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	printf("Source file size: %ld\n", filesize);

	// Allocate the program content in the memory
	char *prgtext = malloc(filesize+1);

	fseek(fp, 0, SEEK_SET);
	fread(prgtext, filesize, 1, fp); 
	prgtext[filesize] = 0; // Add null term to the end of the array
	fclose(fp);

	printf("---\nProgram text:\n|%s|\n---\n\n", prgtext);


	// =============== COMPILE AND EXECUTE ===================
	tfobj *prg = compile(prgtext);

	print_prg(prg);

	// preparativi per chiudere il programma
	destroy_tfobject(prg);

	return 0;
}

/* ======= TO DO LIST: ==========
 * finire il compiler (compile())

 * per finire il compiler serve finire compile() --> (aggiungere i bool e le altre cose)

 * Creare exec(). Deve prendere in input una lista contenente degli oggetti, checkare iterativamente
 * l'oggetto in questione, e in base a quello aggiungerlo alla stack o fare operazioni con oggetti già nella stack 
*/

/*
Mi sa che droppo perché non ci sto più capendo un cazzo
*/

/*
Forse ci sto capendo qualcosa
*/