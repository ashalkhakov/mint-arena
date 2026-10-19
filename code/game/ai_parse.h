/*
 * name:		ai_parse.h
 *
 * desc:		token reading for bot script files
 *
 * The bot AI used to read tokens through trap_PC_ReadToken, which stripped the
 * quotes from string and literal tokens. idlib's precompiler hands tokens back
 * as written, so these wrappers keep the old behaviour for the bot parsers.
 */

#ifndef __AI_PARSE_H__
#define __AI_PARSE_H__

// include after ../idlib/l_script.h and ../idlib/l_precomp.h (they have no include guards)

static ID_INLINE void BotPC_StripQuotes( token_t *token ) {
	if ( token->type == TT_STRING ) {
		StripDoubleQuotes( token->string );
	} else if ( token->type == TT_LITERAL ) {
		StripSingleQuotes( token->string );
	}
}

static ID_INLINE int BotPC_ReadToken( source_t *source, token_t *token ) {
	int ret = PC_ReadToken( source, token );
	BotPC_StripQuotes( token );
	return ret;
}

static ID_INLINE int BotPC_ExpectAnyToken( source_t *source, token_t *token ) {
	int ret = PC_ExpectAnyToken( source, token );
	BotPC_StripQuotes( token );
	return ret;
}

static ID_INLINE int BotPC_ExpectTokenType( source_t *source, int type, int subtype, token_t *token ) {
	int ret = PC_ExpectTokenType( source, type, subtype, token );
	BotPC_StripQuotes( token );
	return ret;
}

#endif
