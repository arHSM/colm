/*
 * Copyright 2001-2018 Adrian Thurston <thurston@colm.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ragel.h"
#include "gotoloop.h"
#include "redfsm.h"
#include "bstmap.h"
#include "gendata.h"
#include "parsedata.h"
#include "inputdata.h"

void SwitchLoopGoto::tableDataPass()
{
	taActions();
	taToStateActions();
	taFromStateActions();
	taEofActions();

	taNfaTargs();
	taNfaOffsets();
	taNfaPushActions();
	taNfaPopTrans();
}

void SwitchLoopGoto::genAnalysis()
{
	/* For directly executable machines there is no required state
	 * ordering. Choose a depth-first ordering to increase the
	 * potential for fall-throughs. */
	redFsm->depthFirstOrdering();

	/* Choose default transitions and the single transition. */
	redFsm->chooseDefaultSpan();
		
	/* Choose single. */
	redFsm->moveSelectTransToSingle();

	/* If any errors have occured in the input file then don't write anything. */
	if ( red->id->errorCount > 0 )
		return;

	/* Anlayze Machine will find the final action reference counts, among other
	 * things. We will use these in reporting the usage of fsm directives in
	 * action code. */
	red->analyzeMachine();

	/* Run the analysis pass over the table data. */
	setTableState( TableArray::AnalyzePass );
	tableDataPass();

	/* Switch the tables over to the code gen mode. */
	setTableState( TableArray::GeneratePass );
}

void SwitchLoopGoto::writeData()
{
	if ( redFsm->anyActions() )
		taActions();

	if ( redFsm->anyToStateActions() )
		taToStateActions();

	if ( redFsm->anyFromStateActions() )
		taFromStateActions();

	if ( redFsm->anyEofActions() )
		taEofActions();

	taNfaTargs();
	taNfaOffsets();
	taNfaPushActions();
	taNfaPopTrans();

	STATE_IDS();
}

std::ostream &SwitchLoopGoto::ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = red->actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numTransRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t" << CASE( STR( act->actionId ) ) << "{\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\t" << CEND() << "}\n";
		}
	}

	return out;
}

std::ostream &SwitchLoopGoto::EOF_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = red->actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numEofRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t" << CASE( STR( act->actionId ) ) << "{\n";
			ACTION( out, act, IlOpts( 0, true, false ) );
			out << "\n\t" << CEND() << "}\n";
		}
	}

	return out;
}

std::ostream &SwitchLoopGoto::FROM_STATE_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = red->actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numFromStateRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t" << CASE( STR( act->actionId ) ) << "{\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\t" << CEND() << "}\n";
		}
	}

	return out;
}

std::ostream &SwitchLoopGoto::TO_STATE_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = red->actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numToStateRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t" << CASE( STR( act->actionId ) ) << "{\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\t" << CEND() << "}\n";
		}
	}

	return out;
}

void SwitchLoopGoto::NFA_PUSH_ACTION( RedNfaTarg *targ )
{
	int act = 0;
	if ( targ->push != 0 )
		act = targ->push->actListId+1;
	nfaPushActions.value( act );
}

void SwitchLoopGoto::NFA_POP_TEST( RedNfaTarg *targ )
{
	int act = 0;
	if ( targ->popTest != 0 )
		act = targ->popTest->actListId+1;
	nfaPopTrans.value( act );
}

std::ostream &SwitchLoopGoto::EXEC_FUNCS()
{
	/* Make labels that set acts and jump to execFuncs. Loop func indicies. */
	for ( GenActionTableMap::Iter redAct = redFsm->actionMap; redAct.lte(); redAct++ ) {
		if ( redAct->numTransRefs > 0 ) {
			out << "	f" << redAct->actListId << ": " <<
				"_acts = " << OFFSET( ARR_REF( actions ), itoa( redAct->location+1 ) ) << ";"
				" goto execFuncs;\n";
		}
	}

	out <<
		"\n"
		"execFuncs:\n";

	if ( redFsm->anyRegNbreak() )
		out << "	_nbreak = 0;\n";

	out <<
		"	_nacts = " << CAST( UINT() ) << DEREF( ARR_REF( actions ), "_acts" ) << ";\n"
		"	_acts += 1;\n"
		"	while ( _nacts > 0 ) {\n"
		"		switch ( " << DEREF( ARR_REF( actions ), "_acts" ) << " ) {\n";
		ACTION_SWITCH() << 
		"		}\n"
		"		_acts += 1;\n"
		"		_nacts -= 1;\n"
		"	}\n"
		"\n";

	if ( redFsm->anyRegNbreak() ) {
		out <<
			"	if ( _nbreak == 1 )\n"
			"		goto _out;\n";
		outLabelUsed = true;
	}

	out <<
		"	goto _again;\n";
	return out;
}

void SwitchLoopGoto::NFA_FROM_STATE_ACTION_EXEC()
{
	if ( redFsm->anyFromStateActions() ) {
		out <<
			"	_acts = " << OFFSET( ARR_REF( actions ), ARR_REF( fromStateActions ) + "[nfa_bp[nfa_len].state]" ) << ";\n"
			"	_nacts = " << CAST( UINT() ) << DEREF( ARR_REF( actions ), "_acts" ) << ";\n"
			"	_acts += 1;\n"
			"	while ( _nacts > 0 ) {\n"
			"		switch ( " << DEREF( ARR_REF( actions ), "_acts" ) << " ) {\n";
			FROM_STATE_ACTION_SWITCH() <<
			"		}\n"
			"		_nacts -= 1;\n"
			"		_acts += 1;\n"
			"	}\n"
			"\n";
	}
}



void SwitchLoopGoto::writeExec()
{
	testEofUsed = false;
	outLabelUsed = false;

	out << "	{\n";

	if ( redFsm->anyRegCurStateRef() )
		out << "	int _ps = 0;\n";

	if ( redFsm->anyToStateActions() || redFsm->anyRegActions() 
			|| redFsm->anyFromStateActions() )
	{
		out << 
			"	" << INDEX( ARR_TYPE( actions ), "_acts" ) << ";\n"
			"	" << UINT() << " _nacts;\n";
	}

	out << "\n";

	if ( redFsm->anyRegNbreak() ) {
		out << "	int _nbreak;\n";
	}

	if ( !noEnd ) {
		testEofUsed = true;
		out << 
			"	if ( " << P() << " == " << PE() << " )\n"
			"		goto _test_eof;\n";
	}

	if ( redFsm->errState != 0 ) {
		outLabelUsed = true;
		out << 
			"	if ( " << vCS() << " == " << redFsm->errState->id << " )\n"
			"		goto _out;\n";
	}

	out << "_resume:\n";

	if ( redFsm->anyFromStateActions() ) {
		out <<
			"	_acts = " << OFFSET( ARR_REF( actions ),
					ARR_REF( fromStateActions ) + "[" + vCS() + "]" ) << ";\n"
			"	_nacts = " << CAST( UINT() ) << DEREF( ARR_REF( actions ), "_acts" ) << "; _acts += 1;\n"
			"	while ( _nacts > 0 ) {\n"
			"		switch ( " << DEREF( ARR_REF( actions ), "_acts" ) << " ) {\n";
			FROM_STATE_ACTION_SWITCH() <<
			"		}\n"
			"		_acts += 1;\n"
			"		_nacts -= 1;\n"
			"	}\n"
			"\n";
	}

	NFA_PUSH();

	out <<
		"	switch ( " << vCS() << " ) {\n";
		STATE_GOTOS() <<
		"	}\n"
		"\n";
		TRANSITIONS() <<
		"\n";

	if ( redFsm->anyRegActions() )
		EXEC_FUNCS() << "\n";

	out << "_again:\n";

	if ( redFsm->anyToStateActions() ) {
		out <<
			"	_acts = " << OFFSET( ARR_REF( actions ),
					ARR_REF( toStateActions ) + "[" + vCS() + "]" ) << ";\n"
			"	_nacts = " << CAST( UINT() ) << DEREF( ARR_REF( actions ), "_acts" ) << "; _acts += 1;\n"
			"	while ( _nacts > 0 ) {\n"
			"		switch ( " << DEREF( ARR_REF( actions ), "_acts" ) << " ) {\n";
			TO_STATE_ACTION_SWITCH() <<
			"		}\n"
			"		_acts += 1;\n"
			"		_nacts -= 1;\n"
			"	}\n"
			"\n";
	}

	if ( redFsm->errState != 0 ) {
		outLabelUsed = true;
		out << 
			"	if ( " << vCS() << " == " << redFsm->errState->id << " )\n"
			"		goto _out;\n";
	}

	if ( !noEnd ) {
		out << 
			"	" << P() << " += 1;\n"
			"	if ( " << P() << " != " << PE() << " )\n"
			"		goto _resume;\n";
	}
	else {
		out << 
			"	" << P() << " += 1;\n"
			"	goto _resume;\n";
	}

	if ( testEofUsed )
		out << "	_test_eof: {}\n";

	if ( redFsm->anyEofTrans() || redFsm->anyEofActions() ) {
		out << 
			"	if ( " << P() << " == " << vEOF() << " )\n"
			"	{\n";

		if ( redFsm->anyEofTrans() ) {
			out <<
				"	switch ( " << vCS() << " ) {\n";

			for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
				if ( st->eofTrans != 0 ) {
					RedCondPair *cond = st->eofTrans->outCond( 0 );
					out << "	case " << st->id << ": goto ctr" << cond->id << ";\n";
				}
			}

			out <<
				"	}\n";
		}

		if ( redFsm->anyEofActions() ) {
			out <<
				"	" << INDEX( ARR_TYPE( actions ), "__acts" ) << ";\n"
				"	" << UINT() << " __nacts;\n"
				"	__acts = " << OFFSET( ARR_REF( actions ), 
						ARR_REF( eofActions ) + "[" + vCS() + "]" ) << ";\n"
				"	__nacts = " << CAST( UINT() ) << DEREF( ARR_REF( actions ), "__acts" ) << "; __acts += 1;\n"
				"	while ( __nacts > 0 ) {\n"
				"		switch ( " << DEREF( ARR_REF( actions ), "__acts" ) << " ) {\n";
				EOF_ACTION_SWITCH() <<
				"		}\n"
				"		__acts += 1;\n"
				"		__nacts -= 1;\n"
				"	}\n";
		}

		out <<
			"	}\n"
			"\n";
	}

	if ( outLabelUsed )
		out << "	_out: {}\n";

	NFA_POP();

	out << "	}\n";
}
