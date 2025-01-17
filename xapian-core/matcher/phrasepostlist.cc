/** @file
 * @brief Return docs containing terms forming a particular phrase.
 */
/* Copyright (C) 2006,2007,2009,2010,2011,2014,2015,2017 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>

#include "phrasepostlist.h"

#include "debuglog.h"
#include "backends/positionlist.h"
#include "omassert.h"
#include "str.h"

#include <algorithm>
#include <vector>

using namespace std;

PhrasePostList::PhrasePostList(PostList *source_,
			       EstimateOp* estimate_op_,
			       Xapian::termpos window_,
			       const vector<PostList*>::const_iterator &terms_begin,
			       const vector<PostList*>::const_iterator &terms_end,
			       PostListTree* pltree_)
    : SelectPostList(source_, estimate_op_, pltree_),
      window(window_),
      terms(terms_begin, terms_end)
{
    size_t n = terms.size();
    Assert(n > 1);
    poslists = new PositionList*[n];
}

PhrasePostList::~PhrasePostList()
{
    delete [] poslists;
}

void
PhrasePostList::start_position_list(unsigned i)
{
    poslists[i] = terms[i]->read_position_list();
}

bool
PhrasePostList::test_doc()
{
    LOGCALL(MATCH, bool, "PhrasePostList::test_doc", NO_ARGS);

    start_position_list(0);
    if (!poslists[0]->next()) {
	++rejected;
	RETURN(false);
    }

    unsigned read_hwm = 0;
    Xapian::termpos b;
    do {
	Xapian::termpos base = poslists[0]->get_position();
	Xapian::termpos pos = base;
	unsigned i = 0;
	do {
	    if (++i == terms.size()) {
		++accepted;
		RETURN(true);
	    }
	    if (i > read_hwm) {
		read_hwm = i;
		start_position_list(i);
	    }
	    if (!poslists[i]->skip_to(pos + 1)) {
		goto reject;
	    }
	    pos = poslists[i]->get_position();
	    b = pos + (terms.size() - i);
	} while (b - base <= window);
	// Advance the start of the window to the first position it could match
	// in given the current position of term i.
    } while (poslists[0]->skip_to(b - window));
reject:
    ++rejected;
    RETURN(false);
}

Xapian::termcount
PhrasePostList::get_wdf() const
{
    // Calculate an estimate for the wdf of a phrase postlist.
    //
    // We use the minimum wdf of a sub-postlist as our estimate.  See the
    // comment in NearPostList::get_wdf() for justification of this estimate.
    vector<PostList *>::const_iterator i = terms.begin();
    Xapian::termcount wdf = (*i)->get_wdf();
    while (++i != terms.end()) {
	wdf = min(wdf, (*i)->get_wdf());
    }
    return wdf;
}

Xapian::doccount
PhrasePostList::get_termfreq() const
{
    // It's hard to estimate how many times the phrase will occur as
    // it depends a lot on the phrase, but usually the phrase will
    // occur significantly less often than the individual terms.
    return pl->get_termfreq() / 3;
}

TermFreqs
PhrasePostList::get_termfreq_est_using_stats(
	const Xapian::Weight::Internal & stats) const
{
    LOGCALL(MATCH, TermFreqs, "PhrasePostList::get_termfreq_est_using_stats", stats);
    // No idea how to estimate this - do the same as get_termfreq() for now.
    TermFreqs result(pl->get_termfreq_est_using_stats(stats));
    result.termfreq /= 3;
    result.reltermfreq /= 3;
    RETURN(result);
}

string
PhrasePostList::get_description() const
{
    string m = "(Phrase ";
    m += str(window);
    m += ' ';
    m += pl->get_description();
    m += ")";
    return m;
}
