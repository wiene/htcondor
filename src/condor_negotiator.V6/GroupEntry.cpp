#include "GroupEntry.h"
#include "condor_common.h"
#include "condor_config.h"
#include "condor_attributes.h"
#include <algorithm>
#include <deque>

GroupEntry::GroupEntry():
		name(),
		config_quota(0),
		static_quota(false),
		accept_surplus(false),
		autoregroup(false),
		usage(0),
		submitterAds(NULL),
		priority(0),
		quota(0),
		requested(0),
		currently_requested(0),
		allocated(0),
		subtree_quota(0),
		subtree_requested(0),
		subtree_usage(0),
		rr(false),
		rr_time(0),
		subtree_rr_time(0),
		parent(NULL),
		children(),
		chmap(),
		sort_ad(new ClassAd()),
		sort_key(0)
{
}

GroupEntry::~GroupEntry() {
		for (unsigned long j=0;  j < children.size();  ++j) {
				if (children[j] != NULL) {
						delete children[j];
				}
		}

		if (NULL != submitterAds) {
				submitterAds->Open();
				while (ClassAd* ad = submitterAds->Next()) {
						submitterAds->Remove(ad);
				}
				submitterAds->Close();

				delete submitterAds;
		}

		if (NULL != sort_ad) delete sort_ad;
}


GroupEntry *
GroupEntry::hgq_construct_tree(
				std::map<std::string, GroupEntry *> &group_entry_map,
				std::vector<GroupEntry *> &hgq_groups,
				bool &global_autoregroup,
				bool &global_accept_surplus) {

		// need to construct group structure
		// groups is list of group names
		// in form group.subgroup group.subgroup.subgroup etc
		char* groupnames = param("GROUP_NAMES");

		// Populate the group array, which contains an entry for each group.
		std::string hgq_root_name = "<none>";
		vector<string> groups;
		if (NULL != groupnames) {
				StringList group_name_list;
				group_name_list.initializeFromString(groupnames);
				group_name_list.rewind();
				while (char* g = group_name_list.next()) {
						const string gname(g);

						// Best to sanity-check this as early as possible.  This will also
						// be useful if we ever decided to allow users to name the root group
						if (gname == hgq_root_name) {
								dprintf(D_ALWAYS, "group quotas: ERROR: group name \"%s\" is reserved for root group -- ignoring this group\n", gname.c_str());
								continue;
						}

						// store the group name
						groups.push_back(gname);
				}

				free(groupnames);
				groupnames = NULL;
		}

		// This is convenient for making sure a parent group always appears before its children
		std::sort(groups.begin(), groups.end(), Accountant::ci_less());

		GroupEntry *hgq_root_group = new GroupEntry;
		hgq_root_group->name = hgq_root_name;
		hgq_root_group->accept_surplus = true;

		group_entry_map.clear();
		group_entry_map[hgq_root_name] = hgq_root_group;

		global_accept_surplus = false;
		global_autoregroup = false;
		const bool default_accept_surplus = param_boolean("GROUP_ACCEPT_SURPLUS", false);
		const bool default_autoregroup = param_boolean("GROUP_AUTOREGROUP", false);
		if (default_autoregroup) global_autoregroup = true;
		if (default_accept_surplus) global_accept_surplus = true;

		// build the tree structure from our group path info
		for (unsigned long j = 0;  j < groups.size();  ++j) {
				string gname = groups[j];

				// parse the group name into a path of sub-group names
				vector<string> gpath;
				parse_group_name(gname, gpath);

				// insert the path of the current group into the tree structure
				GroupEntry* group = hgq_root_group;
				bool missing_parent = false;
				for (unsigned long k = 0;  k < gpath.size()-1;  ++k) {
						// chmap is mostly a structure to avoid n^2 behavior in groups with many children
						map<string, GroupEntry::size_type, Accountant::ci_less>::iterator f(group->chmap.find(gpath[k]));
						if (f == group->chmap.end()) {
								dprintf(D_ALWAYS, "group quotas: WARNING: ignoring group name %s with missing parent %s\n", gname.c_str(), gpath[k].c_str());
								missing_parent = true;
								break;
						}
						group = group->children[f->second];
				}
				if (missing_parent) continue;

				if (group->chmap.count(gpath.back()) > 0) {
						// duplicate group -- ignore
						dprintf(D_ALWAYS, "group quotas: WARNING: ignoring duplicate group name %s\n", gname.c_str());
						continue;
				}

				// enter the new group
				group->children.push_back(new GroupEntry);
				group->chmap[gpath.back()] = group->children.size()-1;
				group_entry_map[gname] = group->children.back();
				group->children.back()->parent = group;
				group = group->children.back();

				// "group" now refers to our current group in the list.
				// Fill in entry values from config.
				group->name = gname;

				// group quota setting
				std::string vname;
				formatstr(vname, "GROUP_QUOTA_%s", gname.c_str());
				double quota = param_double(vname.c_str(), -1.0, 0, INT_MAX);
				if (quota >= 0) {
						group->config_quota = quota;
						group->static_quota = true;
				} else {
						formatstr(vname, "GROUP_QUOTA_DYNAMIC_%s", gname.c_str());
						quota = param_double(vname.c_str(), -1.0, 0.0, 1.0);
						if (quota >= 0) {
								group->config_quota = quota;
								group->static_quota = false;
						} else {
								dprintf(D_ALWAYS, "group quotas: WARNING: no quota specified for group \"%s\", defaulting to zero\n", gname.c_str());
								group->config_quota = 0.0;
								group->static_quota = false;
						}
				}

				// defensive sanity checking
				if (group->config_quota < 0) {
						dprintf(D_ALWAYS, "group quotas: ERROR: negative quota (%g) defaulting to zero\n", double(group->config_quota));
						group->config_quota = 0;
				}

				// accept surplus
				formatstr(vname, "GROUP_ACCEPT_SURPLUS_%s", gname.c_str());
				group->accept_surplus = param_boolean(vname.c_str(), default_accept_surplus);
				formatstr(vname, "GROUP_AUTOREGROUP_%s", gname.c_str());
				group->autoregroup = param_boolean(vname.c_str(), default_autoregroup);
				if (group->autoregroup) global_autoregroup = true;
				if (group->accept_surplus) global_accept_surplus = true;
		}

		// Set the root group's autoregroup state to match the effective global value for autoregroup
		// we do this for the benefit of the accountant, it also can be use to remove some special cases
		// in the negotiator loops.
		hgq_root_group->autoregroup = global_autoregroup;

		// With the tree structure in place, we can make a list of groups in breadth-first order
		// For more convenient iteration over the structure
		hgq_groups.clear();
		std::deque<GroupEntry*> grpq;
		grpq.push_back(hgq_root_group);
		while (!grpq.empty()) {
				GroupEntry* group = grpq.front();
				grpq.pop_front();
				hgq_groups.push_back(group);
				for (vector<GroupEntry*>::iterator j(group->children.begin());  j != group->children.end();  ++j) {
						grpq.push_back(*j);
				}
		}

		string group_sort_expr;
		if (!param(group_sort_expr, "GROUP_SORT_EXPR")) {
				// Should never fail! Default provided via param-info
				EXCEPT("Failed to obtain value for GROUP_SORT_EXPR");
		}
		ExprTree* test_sort_expr = NULL;
		if (ParseClassAdRvalExpr(group_sort_expr.c_str(), test_sort_expr)) {
				EXCEPT("Failed to parse GROUP_SORT_EXPR = %s", group_sort_expr.c_str());
		}
		delete test_sort_expr;
		for (vector<GroupEntry*>::iterator j(hgq_groups.begin());  j != hgq_groups.end();  ++j) {
				GroupEntry* group = *j;
				group->sort_ad->Assign(ATTR_ACCOUNTING_GROUP, group->name);
				// group-specific values might be supported in the future:
				group->sort_ad->AssignExpr(ATTR_SORT_EXPR, group_sort_expr.c_str());
				group->sort_ad->Assign(ATTR_SORT_EXPR_STRING, group_sort_expr);
		}
		return hgq_root_group;
}

void 
GroupEntry::hgq_assign_quotas(double quota) {
		dprintf(D_FULLDEBUG, "group quotas: subtree %s receiving quota= %g\n", this->name.c_str(), quota);

		bool allow_quota_oversub = param_boolean("NEGOTIATOR_ALLOW_QUOTA_OVERSUBSCRIPTION", false);

		// if quota is zero, we can leave this subtree with default quotas of zero
		if (quota <= 0) return;

		// incoming quota is quota for subtree
		this->subtree_quota = quota;

		// compute the sum of any static quotas of any children
		double sqsum = 0;
		double dqsum = 0;
		for (unsigned long j = 0;  j < this->children.size();  ++j) {
				GroupEntry* child = this->children[j];
				if (child->static_quota) {
						sqsum += child->config_quota;
				} else {
						dqsum += child->config_quota;
				}
		}

		// static quotas get first dibs on any available quota
		// total static quota assignable is bounded by quota coming from above
		double sqa = (allow_quota_oversub) ? sqsum : std::min(sqsum, quota);

		// children with dynamic quotas get allocated from the remainder
		double dqa = std::max(0.0, quota - sqa);

		dprintf(D_FULLDEBUG, "group quotas: group %s, allocated %g for static children, %g for dynamic children\n", this->name.c_str(), sqa, dqa);

		// Prevent (0/0) in the case of all static quotas == 0.
		// In this case, all quotas will still be correctly assigned zero.
		double Zs = (sqsum > 0) ? sqsum : 1;

		// If dqsum exceeds 1, then dynamic quota values get scaled so that they sum to 1
		double Zd = std::max(dqsum, double(1));

		// quota assigned to all children
		double chq = 0;
		for (unsigned long j = 0;  j < this->children.size();  ++j) {
				GroupEntry* child = this->children[j];
				// Each child with a static quota gets its proportion of the total of static quota assignable.
				// Each child with dynamic quota gets the dynamic quota assignable weighted by its configured dynamic quota value
				double q = (child->static_quota) ? (child->config_quota * (sqa / Zs)) : (child->config_quota * (dqa / Zd));
				if (q < 0) q = 0;

				if (child->static_quota && (q < child->config_quota)) {
						dprintf(D_ALWAYS, "group quotas: WARNING: static quota for group %s rescaled from %g to %g\n", child->name.c_str(), child->config_quota, q);
				} else if (Zd - 1 > 0.0001) {
						dprintf(D_ALWAYS, "group quotas: WARNING: dynamic quota for group %s rescaled from %g to %g\n", child->name.c_str(), child->config_quota, child->config_quota / Zd);
				}

				child->hgq_assign_quotas(q);
				chq += q;
		}

		// Current group gets anything remaining after assigning to any children
		// If there are no children (a leaf) then this group gets all the quota
		this->quota = (allow_quota_oversub) ? quota : (quota - chq);

		// However, if we are the root ("<none>") group, the "quota" cannot be configured by the
		// admin, and the "quota" represents the entire pool.  We calculate the surplus at any node
		// as the difference between this quota and any demand.  So, if we left the "quota" to be the
		// whole pool, we would be double-counting surplus slots.  Therefore, no matter what allow_quota_oversub
		// is, set the "quota" of the root <none> node (really the limit of usage at exactly this node)
		// to be the total size of the pool, minus the sum allocation of all the child nodes under it, recursively.

		if (this->name == "<none>") {
				this->quota = quota - chq;
		}

		if (this->quota < 0) this->quota = 0;
		dprintf(D_FULLDEBUG, "group quotas: group %s assigned quota= %g\n", this->name.c_str(), this->quota);
}

double 
GroupEntry::hgq_fairshare() {
		dprintf(D_FULLDEBUG, "group quotas: fairshare (1): group= %s  quota= %g  requested= %g\n",
						this->name.c_str(), this->quota, this->requested);

		// Allocate whichever is smallest: the requested slots or group quota.
		this->allocated = std::min(this->requested, this->quota);

		// update requested values
		this->requested -= this->allocated;
		this->subtree_requested = this->requested;

		// surplus quota for this group
		double surplus = this->quota - this->allocated;

		dprintf(D_FULLDEBUG, "group quotas: fairshare (2): group= %s  quota= %g  allocated= %g  requested= %g\n",
						this->name.c_str(), this->quota, this->allocated, this->requested);

		// If this is a leaf group, we're finished: return the surplus
		if (this->children.empty()) {
				return surplus;
		}

		// This is an internal group: perform fairshare recursively on children
		for (unsigned long j = 0;  j < this->children.size();  ++j) {
				GroupEntry* child = this->children[j];
				surplus += child->hgq_fairshare();
				if (child->accept_surplus) {
						this->subtree_requested += child->subtree_requested;
				}
		}

		// allocate any available surplus to current node and subtree
		surplus = this->hgq_allocate_surplus(surplus);

		dprintf(D_FULLDEBUG, "group quotas: fairshare (3): group= %s  surplus= %g  subtree_requested= %g\n",
						this->name.c_str(), surplus, this->subtree_requested);

		// return any remaining surplus up the tree
		return surplus;
}

double
GroupEntry::hgq_allocate_surplus(double surplus) {
		dprintf(D_FULLDEBUG, "group quotas: allocate-surplus (1): group= %s  surplus= %g  subtree-requested= %g\n", this->name.c_str(), surplus, this->subtree_requested);

		// Nothing to allocate
		if (surplus <= 0) return 0;

		// If entire subtree requests nothing, halt now
		if (this->subtree_requested <= 0) return surplus;

		// Surplus allocation policy is that a group shares surplus on equal footing with its children.
		// So we load children and their parent (current group) into a single vector for treatment.
		// Convention will be that current group (subtree root) is last element.
		vector<GroupEntry*> groups(this->children);
		groups.push_back(this);

		// This vector will accumulate allocations.
		// We will proceed with recursive allocations after allocations at this level
		// are completed.  This keeps recursive calls to a minimum.
		vector<double> allocated(groups.size(), 0);

		// Temporarily hacking current group to behave like a child that accepts surplus
		// avoids some special cases below.  Somewhere I just made a kitten cry.
		bool save_accept_surplus = this->accept_surplus;
		this->accept_surplus = true;
		double save_subtree_quota = this->subtree_quota;
		this->subtree_quota = this->quota;
		double requested = this->subtree_requested;
		this->subtree_requested = this->requested;

		if (surplus >= requested) {
				// In this scenario we have enough surplus to satisfy all requests.
				// Cornucopia! Give everybody what they asked for.

				dprintf(D_FULLDEBUG, "group quotas: allocate-surplus (2a): direct allocation, group= %s  requested= %g  surplus= %g\n",
								this->name.c_str(), requested, surplus);

				for (unsigned long j = 0;  j < groups.size();  ++j) {
						GroupEntry* grp = groups[j];
						if (grp->accept_surplus && (grp->subtree_requested > 0)) {
								allocated[j] = grp->subtree_requested;
						}
				}

				surplus -= requested;
				requested = 0;
		} else {
				// In this scenario there are more requests than there is surplus.
				// Here groups have to compete based on their quotas.

				dprintf(D_FULLDEBUG, "group quotas: allocate-surplus (2b): quota-based allocation, group= %s  requested= %g  surplus= %g\n",
								this->name.c_str(), requested, surplus);

				vector<double> subtree_requested(groups.size(), 0);
				for (unsigned long j = 0;  j < groups.size();  ++j) {
						GroupEntry* grp = groups[j];
						// By conditioning on accept_surplus here, I don't have to check it below
						if (grp->accept_surplus && (grp->subtree_requested > 0)) {
								subtree_requested[j] = grp->subtree_requested;
						}
				}

				// In this loop we allocate to groups with quota > 0
				hgq_allocate_surplus_loop(true, groups, allocated, subtree_requested, surplus, requested);

				// Any quota left can be allocated to groups with zero quota
				hgq_allocate_surplus_loop(false, groups, allocated, subtree_requested, surplus, requested);

				// There should be no surplus left after the above two rounds
				if (surplus > 0) {
						dprintf(D_ALWAYS, "group quotas: allocate-surplus WARNING: nonzero surplus %g after allocation\n", surplus);
				}
		}

		// We have computed allocations for groups, with results cached in 'allocated'
		// Now we can perform the actual allocations.  Only actual children should
		// be allocated recursively here
		for (unsigned long j = 0;  j < (groups.size()-1);  ++j) {
				if (allocated[j] > 0) {
						double s = groups[j]->hgq_allocate_surplus(allocated[j]);
						if (fabs(s) > 0.00001) {
								dprintf(D_ALWAYS, "group quotas: WARNING: allocate-surplus (3): surplus= %g\n", s);
						}
				}
		}

		// Here is logic for allocating current group
		this->allocated += allocated.back();
		this->requested -= allocated.back();

		dprintf(D_FULLDEBUG, "group quotas: allocate-surplus (4): group %s allocated surplus= %g  allocated= %g  requested= %g\n",
						this->name.c_str(), allocated.back(), this->allocated, this->requested);

		// restore proper group settings
		this->subtree_requested = requested;
		this->accept_surplus = save_accept_surplus;
		this->subtree_quota = save_subtree_quota;

		return surplus;
}

void round_for_precision(double& x) {
		double ref = x;
		x = floor(0.5 + x);
		double err = fabs(x-ref);
		// This error threshold is pretty ad-hoc.  It would be ideal to try and figure out
		// bounds on precision error accumulation based on size of HGQ tree.
		if (err > 0.00001) {
				// If precision errors are not small, I am suspicious.
				dprintf(D_ALWAYS, "group quotas: WARNING: encountered precision error of %g\n", err);
		}
}

double
GroupEntry::hgq_recover_remainders() {
		dprintf(D_FULLDEBUG, "group quotas: recover-remainders (1): group= %s  allocated= %g  requested= %g\n",
						this->name.c_str(), this->allocated, this->requested);

		// recover fractional remainder, which becomes surplus
		double surplus = this->allocated - floor(this->allocated);
		this->allocated -= surplus;
		this->requested += surplus;

		// These should be integer values now, so I get to round to correct any precision errs
		round_for_precision(this->allocated);
		round_for_precision(this->requested);

		this->subtree_requested = this->requested;
		this->subtree_rr_time = (this->requested > 0) ? this->rr_time : DBL_MAX;

		dprintf(D_FULLDEBUG, "group quotas: recover-remainders (2): group= %s  allocated= %g  requested= %g  surplus= %g\n",
						this->name.c_str(), this->allocated, this->requested, surplus);

		// If this is a leaf group, we're finished: return the surplus
		if (this->children.empty()) return surplus;

		// This is an internal group: perform recovery recursively on children
		for (unsigned long j = 0;  j < this->children.size();  ++j) {
				GroupEntry* child = this->children[j];
				surplus += child->hgq_recover_remainders();
				if (child->accept_surplus) {
						this->subtree_requested += child->subtree_requested;
						if (child->subtree_requested > 0)
								this->subtree_rr_time = std::min(this->subtree_rr_time, child->subtree_rr_time);
				}
		}

		// allocate any available surplus to current node and subtree
		surplus = this->hgq_round_robin(surplus);

		dprintf(D_FULLDEBUG, "group quotas: recover-remainder (3): group= %s  surplus= %g  subtree_requested= %g\n",
						this->name.c_str(), surplus, this->subtree_requested);

		// return any remaining surplus up the tree
		return surplus;
}

double
GroupEntry::hgq_round_robin(double surplus) {
		dprintf(D_FULLDEBUG, "group quotas: round-robin (1): group= %s  surplus= %g  subtree-requested= %g\n", this->name.c_str(), surplus, this->subtree_requested);

		// Sanity check -- I expect these to be integer values by the time I get here.
		if (this->subtree_requested != floor(this->subtree_requested)) {
				dprintf(D_ALWAYS, "group quotas: WARNING: forcing group %s requested= %g to integer value %g\n",
								this->name.c_str(), this->subtree_requested, floor(this->subtree_requested));
				this->subtree_requested = floor(this->subtree_requested);
		}

		// Nothing to do if subtree had no requests
		if (this->subtree_requested <= 0) return surplus;

		// round robin has nothing to do without at least one whole slot
		if (surplus < 1) return surplus;

		// Surplus allocation policy is that a group shares surplus on equal footing with its children.
		// So we load children and their parent (current group) into a single vector for treatment.
		// Convention will be that current group (subtree root) is last element.
		vector<GroupEntry*> groups(this->children);
		groups.push_back(this);

		// This vector will accumulate allocations.
		// We will proceed with recursive allocations after allocations at this level
		// are completed.  This keeps recursive calls to a minimum.
		vector<double> allocated(groups.size(), 0);

		// Temporarily hacking current group to behave like a child that accepts surplus
		// avoids some special cases below.  Somewhere I just made a kitten cry.  Even more.
		bool save_accept_surplus = this->accept_surplus;
		this->accept_surplus = true;
		double save_subtree_quota = this->subtree_quota;
		this->subtree_quota = this->quota;
		double save_subtree_rr_time = this->subtree_rr_time;
		this->subtree_rr_time = this->rr_time;
		double requested = this->subtree_requested;
		this->subtree_requested = this->requested;

		double outstanding = 0;
		vector<double> subtree_requested(groups.size(), 0);
		for (unsigned long j = 0;  j < groups.size();  ++j) {
				GroupEntry* grp = groups[j];
				if (grp->accept_surplus && (grp->subtree_requested > 0)) {
						subtree_requested[j] = grp->subtree_requested;
						outstanding += 1;
				}
		}

		// indexes allow indirect sorting
		vector<unsigned long> idx(groups.size());
		for (unsigned long j = 0;  j < idx.size();  ++j) idx[j] = j;

		// order the groups to determine who gets first cut
		ord_by_rr_time ord;
		ord.data = &groups;
		std::sort(idx.begin(), idx.end(), ord);

		while ((surplus >= 1) && (requested > 0)) {
				// max we can fairly allocate per group this round:
				double amax = std::max(double(1), floor(surplus / outstanding));

				dprintf(D_FULLDEBUG, "group quotas: round-robin (2): pass: surplus= %g  requested= %g  outstanding= %g  amax= %g\n",
								surplus, requested, outstanding, amax);

				outstanding = 0;
				double sumalloc = 0;
				for (unsigned long jj = 0;  jj < groups.size();  ++jj) {
						unsigned long j = idx[jj];
						GroupEntry* grp = groups[j];
						if (grp->accept_surplus && (subtree_requested[j] > 0)) {
								double a = std::min(subtree_requested[j], amax);
								allocated[j] += a;
								subtree_requested[j] -= a;
								sumalloc += a;
								surplus -= a;
								requested -= a;
								grp->rr = true;
								if (subtree_requested[j] > 0) outstanding += 1;
								if (surplus < amax) break;
						}
				}

				// a bit of defensive sanity checking -- should not be possible:
				if (sumalloc < 1) {
						dprintf(D_ALWAYS, "group quotas: round-robin (3): WARNING: round robin failed to allocate >= 1 slot this round - halting\n");
						break;
				}
		}

		// We have computed allocations for groups, with results cached in 'allocated'
		// Now we can perform the actual allocations.  Only actual children should
		// be allocated recursively here
		for (unsigned long j = 0;  j < (groups.size()-1);  ++j) {
				if (allocated[j] > 0) {
						double s = groups[j]->hgq_round_robin(allocated[j]);

						// This algorithm does not allocate more than a child has requested.
						// Also, this algorithm is designed to allocate every requested slot,
						// up to the given surplus.  Therefore, I expect these calls to return
						// zero.   If they don't, something is haywire.
						if (s > 0) {
								dprintf(D_ALWAYS, "group quotas: round-robin (4):  WARNING: nonzero surplus %g returned from round robin for group %s\n",
												s, groups[j]->name.c_str());
						}
				}
		}

		// Here is logic for allocating current group
		this->allocated += allocated.back();
		this->requested -= allocated.back();

		dprintf(D_FULLDEBUG, "group quotas: round-robin (5): group %s allocated surplus= %g  allocated= %g  requested= %g\n",
						this->name.c_str(), allocated.back(), this->allocated, this->requested);

		// restore proper group settings
		this->subtree_requested = requested;
		this->accept_surplus = save_accept_surplus;
		this->subtree_quota = save_subtree_quota;
		this->subtree_rr_time = save_subtree_rr_time;

		return surplus;
}


void hgq_allocate_surplus_loop(bool by_quota,
				vector<GroupEntry*>& groups, vector<double>& allocated, vector<double>& subtree_requested,
				double& surplus, double& requested)
{
		int iter = 0;
		while (surplus > 0) {
				iter += 1;

				dprintf(D_FULLDEBUG, "group quotas: allocate-surplus-loop: by_quota= %d  iteration= %d  requested= %g  surplus= %g\n",
								int(by_quota), iter, requested, surplus);

				// Compute the normalizer for outstanding groups
				double Z = 0;
				for (unsigned long j = 0;  j < groups.size();  ++j) {
						GroupEntry* grp = groups[j];
						if (subtree_requested[j] > 0)  Z += (by_quota) ? grp->subtree_quota : 1.0;
				}

				if (Z <= 0) {
						dprintf(D_FULLDEBUG, "group quotas: allocate-surplus-loop: no further outstanding groups at iteration %d - halting.\n", iter);
						break;
				}

				// allocations
				bool never_gt = true;
				double sumalloc = 0;
				for (unsigned long j = 0;  j < groups.size();  ++j) {
						GroupEntry* grp = groups[j];
						if (subtree_requested[j] > 0) {
								double N = (by_quota) ? grp->subtree_quota : 1.0;
								double a = surplus * (N / Z);
								if (a > subtree_requested[j]) {
										a = subtree_requested[j];
										never_gt = false;
								}
								allocated[j] += a;
								subtree_requested[j] -= a;
								sumalloc += a;
						}
				}

				surplus -= sumalloc;
				requested -= sumalloc;

				// Compensate for numeric precision jitter
				// This is part of the convergence guarantee: on each iteration, one of two things happens:
				// either never_gt becomes true, in which case all surplus was allocated, or >= 1 group had its
				// requested drop to zero.  This will move us toward Z becoming zero, which will halt the loop.
				// Note, that in "by-quota" mode, Z can become zero with surplus remaining, which is fine -- it means
				// groups with quota > 0 did not use all the surplus, and any groups with zero quota have the option
				// to use it in "non-by-quota" mode.
				if (never_gt || (surplus < 0)) {
						if (fabs(surplus) > 0.00001) {
								dprintf(D_ALWAYS, "group quotas: allocate-surplus-loop: WARNING: rounding surplus= %g to zero\n", surplus);
						}
						surplus = 0;
				}
		}
}
