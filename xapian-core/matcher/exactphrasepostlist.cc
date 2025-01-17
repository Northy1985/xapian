/** @file
 * @brief Return docs containing terms forming a particular exact phrase.
 */
/* Copyright (C) 2006-2022 Olly Betts
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

#include "exactphrasepostlist.h"

#include "debuglog.h"
#include "backends/positionlist.h"
#include "omassert.h"

#include <algorithm>
#include <vector>

using namespace std;

ExactPhrasePostList::ExactPhrasePostList(PostList *source_,
					 EstimateOp* estimate_op_,
					 const vector<PostList*>::const_iterator &terms_begin,
					 const vector<PostList*>::const_iterator &terms_end,
					 PostListTree* pltree_)
    : SelectPostList(source_, estimate_op_, pltree_),
      terms(terms_begin, terms_end)
{
    size_t n = terms.size();
    Assert(n > 1);
    poslists = new PositionList*[n];
    try {
	order = new unsigned[n];
    } catch (...) {
	delete [] poslists;
	throw;
    }
    for (size_t i = 0; i < n; ++i) order[i] = unsigned(i);
}

ExactPhrasePostList::~ExactPhrasePostList()
{
    delete [] poslists;
    delete [] order;
}

void
ExactPhrasePostList::start_position_list(unsigned i)
{
    poslists[i] = terms[order[i]]->read_position_list();
}

class TermCompare {
    vector<PostList *> & terms;

  public:
    explicit TermCompare(vector<PostList *> & terms_) : terms(terms_) { }

    bool operator()(unsigned a, unsigned b) const {
	return terms[a]->get_wdf() < terms[b]->get_wdf();
    }
};

bool
ExactPhrasePostList::test_doc()
{
    LOGCALL(MATCH, bool, "ExactPhrasePostList::test_doc", NO_ARGS);

    // We often don't need to read all the position lists, so rather than using
    // the shortest position lists first, we approximate by using the terms
    // with the lowest wdf first.  This will typically give the same or a very
    // similar order.
    sort(order, order + terms.size(), TermCompare(terms));

    // If the first term we check only occurs too close to the start of the
    // document, we only need to read one term's positions.  E.g. search for
    // "ripe mango" when the only occurrence of 'mango' in the current document
    // is at position 0.
    start_position_list(0);
    if (!poslists[0]->skip_to(order[0])) {
	++rejected;
	RETURN(false);
    }

    // If we get here, we'll need to read the positionlists for at least two
    // terms, so check the true positionlist length for the two terms with the
    // lowest wdf and if necessary swap them so the true shorter one is first.
    start_position_list(1);
    if (poslists[0]->get_approx_size() > poslists[1]->get_approx_size()) {
	if (!poslists[1]->skip_to(order[1])) {
	    ++rejected;
	    RETURN(false);
	}
	swap(poslists[0], poslists[1]);
	swap(order[0], order[1]);
    }

    unsigned read_hwm = 1;
    Xapian::termpos idx0 = order[0];
    Xapian::termpos base = poslists[0]->get_position() - idx0;
    unsigned i = 1;
    while (true) {
	if (i > read_hwm) {
	    read_hwm = i;
	    start_position_list(i);
	    // FIXME: consider comparing with poslist[0] and swapping
	    // if less common.  Should we allow for the number of positions
	    // we've read from poslist[0] already?
	}
	Xapian::termpos idx = order[i];
	Xapian::termpos required = base + idx;
	if (!poslists[i]->skip_to(required))
	    break;
	Xapian::termpos got = poslists[i]->get_position();
	if (got == required) {
	    if (++i == terms.size()) {
		++accepted;
		RETURN(true);
	    }
	    continue;
	}
	if (!poslists[0]->skip_to(got - idx + idx0))
	    break;
	base = poslists[0]->get_position() - idx0;
	i = 1;
    }
    ++rejected;
    RETURN(false);
}

Xapian::termcount
ExactPhrasePostList::get_wdf() const
{
    // Calculate an estimate for the wdf of an exact phrase postlist.
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
ExactPhrasePostList::get_termfreq() const
{
    // It's hard to estimate how many times the exact phrase will occur as
    // it depends a lot on the phrase, but usually the exact phrase will
    // occur significantly less often than the individual terms.
    //
    // We divide by 4 here rather than by 2 as we do for NearPostList and
    // PhrasePostList, as a very rough heuristic to represent the fact that the
    // words must occur exactly in order, and phrases are therefore rarer than
    // near matches and (non-exact) phrase matches.
    return pl->get_termfreq() / 4;
}

TermFreqs
ExactPhrasePostList::get_termfreq_est_using_stats(
	const Xapian::Weight::Internal & stats) const
{
    LOGCALL(MATCH, TermFreqs, "ExactPhrasePostList::get_termfreq_est_using_stats", stats);
    // No idea how to estimate this - do the same as get_termfreq() for now.
    TermFreqs result(pl->get_termfreq_est_using_stats(stats));
    result.termfreq /= 4;
    result.reltermfreq /= 4;
    RETURN(result);
}

string
ExactPhrasePostList::get_description() const
{
    return "(ExactPhrase " + pl->get_description() + ")";
}
