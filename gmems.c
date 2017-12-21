//
// Copyright (C) 2010-2017 Poggi Valerio
//
// The GMEMS Viewer is free software: you can redistribute
// it and/or modify it under the terms of the GNU Affero General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// GMEMS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// with this download. If not, see <http://www.gnu.org/licenses/>
//
// Author: Poggi Valerio

# include <stdio.h>
# include <stdlib.h>
# include <pthread.h>
# include <signal.h>
# include <unistd.h>
# include <fcntl.h>
# include <math.h>
# include <sys/ioctl.h>
# include <linux/joystick.h>
# include <gtk/gtk.h>
# include <cairo.h>

# define JOY_DEV "/dev/input/js0"
# define MAX_AXES 8

# define WIN_H 600
# define WIN_W 1000

# define T_LEN 500
# define GAIN 30

# define X_NUM 10
# define Y_NUM 30

//------------------------------------------
// Variable declaration

static int smp[3];
static int ch0[T_LEN];
static int ch1[T_LEN];
static int ch2[T_LEN];
static double mn0, mn1, mn2;

static int li[X_NUM];

static int joy_fd;
static int num_of_axis = 0;
static int num_of_buttons = 0;
static char name_of_joystick[80];

struct js_event js;
struct js_corr corr[MAX_AXES];

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

static GdkPixmap *pixmap = NULL;
static int currently_drawing = 0;

//------------------------------------------

void *get_joy_data ()
{
  int s;

  //------------------------------------------
  // Read the joystick state
  while (1)
  {
    s = read(joy_fd, &js, sizeof(struct js_event));
    if (s > 0)
      smp[js.number] = js.value;

    // (update every 10 microseconds)
    usleep(10);
  }
}

//------------------------------------------

void *update_data ()
{
  int i;

  //------------------------------------------
  // Fill the data buffer (blocking mode)
  while (1)
  {
    // Synchronize threads
    pthread_mutex_lock( &mutex1 );

    mn0 = 0;
    mn1 = 0;
    mn2 = 0;

    for ( i = T_LEN-1; i >= 0; i-- )
    {
      if (i != 0)
      {
        // Shift trace buffer of one element
        ch0[i] = ch0[i-1];
        ch1[i] = ch1[i-1];
        ch2[i] = ch2[i-1];
      }
      else
      {
        // Update with current sample
        ch0[0] = smp[0];
        ch1[0] = smp[1];
        ch2[0] = smp[2];
      }
      // Compute mean
      mn0 = mn0 + ch0[i];
      mn1 = mn1 + ch1[i];
      mn2 = mn2 + ch2[i];
    }

    mn0 = mn0/T_LEN;
    mn1 = mn1/T_LEN;
    mn2 = mn2/T_LEN;

    // printf("%d %d %d\n",ch0[0],ch1[0],ch2[0]);
    // fflush(stdout);

    // Update horizontal line index
    for ( i = 0; i < X_NUM; i++ )
    {
      if ( li[i] != (T_LEN-1) )
        li[i]++;
      else
        li[i] = 0;
    }

    // Synchronize threads
    pthread_mutex_unlock( &mutex1 );

    // (update every 8 milliseconds)
    usleep(8000);
  }
}

//------------------------------------------

gboolean on_window_configure_event (GtkWidget *da, GdkEventConfigure *event, gpointer user_data)
{
  static int oldw = 0;
  static int oldh = 0;

  //------------------------------------------
  // Initialize a global buffer pixmap

  if (oldw != event->width || oldh != event->height)
  {
    // Create a new pixmap with the correct size
    GdkPixmap *tmppixmap = gdk_pixmap_new(da->window, event->width, event->height, -1);

    int minw = oldw;
    int minh = oldh;

    if ( event->width < minw )
      minw =  event->width;
    if ( event->height < minh )
      minh =  event->height;

    // Copy the contents of the old pixmap to the new pixmaps
    gdk_draw_drawable(tmppixmap, da->style->fg_gc[GTK_WIDGET_STATE(da)],pixmap, 0, 0, 0, 0, minw, minh);

    g_object_unref(pixmap); 
    pixmap = tmppixmap;
  }

  oldw = event->width;
  oldh = event->height;

  return TRUE;
}

//------------------------------------------

gboolean on_window_expose_event (GtkWidget *da, GdkEventExpose *event, gpointer user_data)
{
  //------------------------------------------
  // Only copy the area that was exposed

  gdk_draw_drawable(da->window,
                    da->style->fg_gc[GTK_WIDGET_STATE(da)], pixmap,
                    event->area.x, event->area.y,
                    event->area.x, event->area.y,
                    event->area.width, event->area.height);

  return TRUE;
}

//------------------------------------------

void *do_draw (void *ptr)
{
  int i;

  //------------------------------------------
  // Execute do_draw in a new thread when
  // animation updating is needed

  // Prepare to trap SIGALRM
  siginfo_t info;
  sigset_t sigset;

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGALRM);

  int width, height;

  while(1)
  {
    // Wait for our SIGALRM
    while (sigwaitinfo(&sigset, &info) > 0)
    {
      currently_drawing = 1;

      gdk_threads_enter();
      gdk_drawable_get_size(pixmap, &width, &height);
      gdk_threads_leave();

      // Create a gtk-independant surface to draw on
      cairo_surface_t *cst = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
      cairo_t *cr = cairo_create(cst);

      //------------------------------------------
      // Main drawing

      double SF;
      SF = ((double)width-150)/T_LEN;

      //------------------------------------------
      // Background

      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_paint(cr);

      cairo_set_source_rgb(cr, 0.9, 0.6, 0.3);
      cairo_rectangle(cr, 0, 0, 100, height);
      cairo_fill(cr);

      cairo_pattern_t *pat1;

      pat1 = cairo_pattern_create_linear(100, 0,  400, 0);
      cairo_pattern_add_color_stop_rgb(pat1, 0, 0.6, 0.6, 0.6);
      cairo_pattern_add_color_stop_rgb(pat1, 1, 1, 1, 1);

      cairo_rectangle(cr, 100, 0, 300, height);
      cairo_set_source(cr, pat1);
      cairo_fill(cr);

      pat1 = cairo_pattern_create_linear(width, 0,  width-300, 0);
      cairo_pattern_add_color_stop_rgb(pat1, 0, 0.6, 0.6, 0.6);
      cairo_pattern_add_color_stop_rgb(pat1, 1, 1, 1, 1);

      cairo_rectangle(cr, width-300, 0, 300, height);
      cairo_set_source(cr, pat1);
      cairo_fill(cr);

      cairo_pattern_destroy(pat1);

      //------------------------------------------
      // Horizontal and Vertical strips

      cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
      cairo_set_line_width(cr, 1);

      for ( i = 0; i < Y_NUM; i++ )
      {
        cairo_move_to(cr, 100, i*(double)height/(Y_NUM-1));
        cairo_line_to(cr, (double)width, i*(double)height/(Y_NUM-1));
      }

      for ( i = 0; i < X_NUM; i++ )
      {
        cairo_move_to(cr, SF*li[i]+150, 0);
        cairo_line_to(cr, SF*li[i]+150, (double)height);
      }
      cairo_stroke(cr);

      //------------------------------------------
      // Text

      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_select_font_face(cr, "Courier", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 30);

      cairo_move_to(cr, 20, 10+(double)height/6);
      cairo_show_text(cr, "N-S");

      cairo_move_to(cr, 20, 10+3*(double)height/6);
      cairo_show_text(cr, "E-W");

      cairo_move_to(cr, 20, 10+5*(double)height/6);
      cairo_show_text(cr, "U-D");

      //------------------------------------------

      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_set_line_width(cr, 10);
      cairo_move_to(cr, 100, 0);
      cairo_line_to(cr, 100, width);
      cairo_stroke(cr);

      //------------------------------------------
      // Trace draw

      // Synchronize threads
      pthread_mutex_lock( &mutex1 );

      //------------------------------------------
      // CHANNEL 0

      cairo_set_source_rgb(cr, 1, 0, 0);
      cairo_set_line_width(cr, 3);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 150, (ch0[0]-mn0)/GAIN+(double)height/6);
      for ( i = 0; i < T_LEN-1; i++ )
        cairo_line_to(cr, SF*(i+1)+150, (ch0[i+1]-mn0)/GAIN+(double)height/6);
      cairo_stroke(cr);

      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_set_line_width(cr, 5);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 100, (ch0[0]-mn0)/GAIN+(double)height/6-10);
      cairo_line_to(cr, 150, (ch0[0]-mn0)/GAIN+(double)height/6);
      cairo_line_to(cr, 100, (ch0[0]-mn0)/GAIN+(double)height/6+10);
      cairo_stroke(cr);

      //------------------------------------------
      // CHANNEL 1

      cairo_set_source_rgb(cr, 0, 1, 0);
      cairo_set_line_width(cr, 3);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 150, (ch1[0]-mn1)/GAIN+3*(double)height/6);
      for ( i = 0; i < T_LEN-1; i++ )
        cairo_line_to(cr, SF*(i+1)+150, (ch1[i+1]-mn1)/GAIN+3*(double)height/6);
      cairo_stroke(cr);

      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_set_line_width(cr, 5);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 100, (ch1[0]-mn1)/GAIN+3*(double)height/6-10);
      cairo_line_to(cr, 150, (ch1[0]-mn1)/GAIN+3*(double)height/6);
      cairo_line_to(cr, 100, (ch1[0]-mn1)/GAIN+3*(double)height/6+10);
      cairo_stroke(cr);

      //------------------------------------------
      // CHANNEL 2

      cairo_set_source_rgb(cr, 0, 0, 1);
      cairo_set_line_width(cr, 3);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 150, (ch2[0]-mn2)/GAIN+5*(double)height/6);
      for ( i = 0; i < T_LEN-1; i++ )
        cairo_line_to(cr, SF*(i+1)+150, (ch2[i+1]-mn2)/GAIN+5*(double)height/6);
      cairo_stroke(cr);

      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_set_line_width(cr, 5);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      cairo_move_to(cr, 100, (ch2[0]-mn2)/GAIN+5*(double)height/6-10);
      cairo_line_to(cr, 150, (ch2[0]-mn2)/GAIN+5*(double)height/6);
      cairo_line_to(cr, 100, (ch2[0]-mn2)/GAIN+5*(double)height/6+10);
      cairo_stroke(cr);

      // Synchronize threads
      pthread_mutex_unlock( &mutex1 );

      cairo_destroy(cr);

      //------------------------------------------

      gdk_threads_enter();

      cairo_t *cr_pixmap = gdk_cairo_create(pixmap);
      cairo_set_source_surface (cr_pixmap, cst, 0, 0);
      cairo_paint(cr_pixmap);
      cairo_destroy(cr_pixmap);

      gdk_threads_leave();

      cairo_surface_destroy(cst);

      currently_drawing = 0;
    }
  }
}

//------------------------------------------

gboolean timer_exe (GtkWidget * window)
{
  //------------------------------------------
  // Synchronize the drawing threads

  static int first_time = 1;
  int drawing_status = g_atomic_int_get(&currently_drawing);

  static pthread_t thread_info;
  if(first_time == 1)
  {
    int  iret;
    iret = pthread_create( &thread_info, NULL, do_draw, NULL);
  }

  if(drawing_status == 0)
  {
    pthread_kill(thread_info, SIGALRM);
  }

  int width, height;

  // Tell the window to draw the animation
  gdk_drawable_get_size(pixmap, &width, &height);
  gtk_widget_queue_draw_area(window, 0, 0, width, height);

  first_time = 0;
  return TRUE;
}

//------------------------------------------

int main (int argc, char *argv[])
{
  int i,j;

  //------------------------------------------
  // Initialize horizontal lines

  for ( i = 0; i < X_NUM; i++ )
    li[i] = i*T_LEN/(X_NUM-1);

  //------------------------------------------
  // Configuring USB device (MEMS sensor)

  if ((joy_fd = open(JOY_DEV, O_RDONLY)) == -1)
  {
    printf("Sensor not found...\n");
    exit(EXIT_FAILURE);
  }

  ioctl(joy_fd, JSIOCGAXES, &num_of_axis);
  ioctl(joy_fd, JSIOCGBUTTONS, &num_of_buttons);
  ioctl(joy_fd, JSIOCGNAME(80), &name_of_joystick);

  // Use non-blocking mode
  fcntl(joy_fd, F_SETFL, O_NONBLOCK );

  //------------------------------------------
  // Setting coefficient structure to zero
  // and all axes to "Raw Mode"

  for (i=0; i<MAX_AXES; i++)
  {
    corr[i].type = JS_CORR_NONE;
    corr[i].prec = 0;
    for (j=0; j<8; j++)
    {
      corr[i].coef[j] = 0;
    }
  }

  if (ioctl(joy_fd, JSIOCSCORR, &corr))
  {
    printf("Error setting correction...\n");
    exit(EXIT_FAILURE);
  }

  //------------------------------------------
  // Creating non-blocking thread to
  // periodically empty the MEMS data buffer

  pthread_t thread1, thread2;

  pthread_create(&thread1, NULL, get_joy_data, NULL);
  pthread_tryjoin_np(thread1, NULL);

  pthread_create(&thread2, NULL, update_data, NULL);
  pthread_tryjoin_np(thread2, NULL);

  //------------------------------------------
  // Block SIGALRM in the main thread

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGALRM);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  //------------------------------------------
  // GTK initialization

  // Make GTK thread-aware
  if (!g_thread_supported ()){ g_thread_init(NULL); }
  gdk_threads_init();
  gdk_threads_enter();

  gtk_init(&argc, &argv);

  GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  gtk_window_set_default_size (GTK_WINDOW (window), WIN_W, WIN_H);
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
  gtk_window_set_title (GTK_WINDOW (window), "Real Time Acceleration");

  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(G_OBJECT(window), "expose_event", G_CALLBACK(on_window_expose_event), NULL);
  g_signal_connect(G_OBJECT(window), "configure_event", G_CALLBACK(on_window_configure_event), NULL);

  gtk_widget_show_all(window);

  // Set up pixmap
  pixmap = gdk_pixmap_new(window->window,WIN_W,WIN_H,-1);

  // Turn off gtk's automatic painting and double buffering routines
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_widget_set_double_buffered(window, FALSE);

  (void)g_timeout_add(32, (GSourceFunc)timer_exe, window);

  gtk_main();
  gdk_threads_leave();

  return 0;
}

