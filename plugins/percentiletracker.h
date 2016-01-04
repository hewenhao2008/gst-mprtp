/*
 * percentiletracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PERCENTILETRACKER_H_
#define PERCENTILETRACKER_H_

#include <gst/gst.h>

typedef struct _PercentileTracker PercentileTracker;
typedef struct _PercentileTrackerClass PercentileTrackerClass;
typedef struct _PercentileState PercentileState;
#include "bintree.h"

#define PERCENTILETRACKER_TYPE             (percentiletracker_get_type())
#define PERCENTILETRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PERCENTILETRACKER_TYPE,PercentileTracker))
#define PERCENTILETRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PERCENTILETRACKER_TYPE,PercentileTrackerClass))
#define PERCENTILETRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PERCENTILETRACKER_TYPE))
#define PERCENTILETRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PERCENTILETRACKER_TYPE))
#define PERCENTILETRACKER_CAST(src)        ((PercentileTracker *)(src))

typedef struct _PercentileTrackerItem PercentileTrackerItem;
struct _PercentileTracker
{
  GObject                  object;
  BinTree*                 mintree;
  BinTree*                 maxtree;
  GRWLock                  rwmutex;
  PercentileTrackerItem*       items;
  gboolean                 debug;
  guint64                  sum;
  guint8                   percentile;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gdouble                  ratio;

  guint                    required;
  PercentileState*         state;
};

struct _PercentileTrackerItem
{
  guint64       value;
  GstClockTime  added;
};

struct _PercentileTrackerClass{
  GObjectClass parent_class;

};

GType percentiletracker_get_type (void);
PercentileTracker *make_percentiletracker(
                                  guint32 length,
                                  guint percentile);

PercentileTracker *make_percentiletracker_debug(
                                  guint32 length,
                                  guint percentile);

PercentileTracker *make_percentiletracker_full(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile);

void percentiletracker_test(void);
void percentiletracker_set_treshold(PercentileTracker *this, GstClockTime treshold);
guint32 percentiletracker_get_num(PercentileTracker *this);
guint64 percentiletracker_get_last(PercentileTracker *this);
guint64
percentiletracker_get_stats (PercentileTracker * this,
                         guint64 *min,
                         guint64 *max,
                         guint64 *sum);
void percentiletracker_obsolate (PercentileTracker * this);
void percentiletracker_reset(PercentileTracker *this);
void percentiletracker_add(PercentileTracker *this, guint64 value);

#endif /* PERCENTILETRACKER_H_ */