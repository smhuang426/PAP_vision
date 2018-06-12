// APA.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdio.h>
#include <tchar.h>
#include <opencv2/opencv.hpp>    
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
//#include <opencv/cv.h>

using namespace cv;
using namespace std;

#define DEBUG 1

//=====================
// Declare Macro
//=====================
#define WHITE_COLOR			255

// Parameter setting
#define KERNEL_SIZE			7
#define BINARY_THRESHOLD	200

#define CANNY_LOW_THRLD		50
#define CANNY_HIGH_THRLD	3 * CANNY_LOW_THRLD

#define REAL_BLOCK_WIDTH	1000 // (mm)
#define REAL_BLOCK_HEIGHT	1000 // (mm)

#define PARKING_LINE_WIDTH	200  // (mm)

//=====================
// Declare Variable
//=====================
Point2f src_points[] = {
	Point2f(198, 65),
	Point2f(294, 68),
	Point2f(186, 105),
	Point2f(302, 108) };

Point2f dst_points[] = {
	Point2f(198, 65),
	Point2f(294, 68),
	Point2f(198, 105 + 47),
	Point2f(294, 108 + 47) };

// Variables for mouse event
bool is_object_selected = false;
Rect select_region;
Point start_point;
int selected_times = 0;

float width_len_per_pix  = 0;
float height_len_per_pix = 0;

//=====================
// Declare struct & enum
//=====================
enum APA_Orientation
{
	ERR_ORIENT = -1,
	X_ORIENT   = 0,   // normal case
	Y_ORIENT   = 1,   // use it when slope is inf, so change orientation.
};

typedef struct
{
	float slope;
	float bias;
	APA_Orientation orient;
} Poly_1D;

typedef struct
{
	int orientation;
	Mat src_img;
	vector<Vec2i> src_points;
} Mouse_event_prm;

//=====================
// Declare Method
//=====================
Mat Binarization(Mat input_img, Size kernel_size, int thrld)
{
	Mat ret;
	GaussianBlur(input_img, ret, kernel_size, 0, 0, 0);
	threshold(ret, ret, thrld, WHITE_COLOR, 0);
	return ret;
}

Mat Canny_Process(Mat input_img, int low_thr, int high_thr, int kernel_size)
{
	Mat canny_ret;

	GaussianBlur(input_img, canny_ret, Size(kernel_size, kernel_size), 0, 0, 0);
	Canny(canny_ret, canny_ret, low_thr, high_thr, kernel_size);

	return canny_ret;
}

void Poly_fit_one_order(float x1, float y1, float x2, float y2, Poly_1D *poly)
{
	float diff_x = x2 - x1;
	float diff_y = y2 - y1;

	poly->orient = ERR_ORIENT;
	poly->slope = 0;
	poly->bias = 0;

	if (abs(diff_x) > 0.0001f)
	{
		if (abs(diff_y / diff_x) > 1.0f)
		{
			poly->orient = Y_ORIENT;
			poly->slope = diff_x / diff_y;
			poly->bias = x2 - (poly->slope) * y2;
		}
		else
		{
			poly->orient = X_ORIENT;
			poly->slope = diff_y / diff_x;
			poly->bias = y2 - (poly->slope) * x2;
		}

		return;
	}

	if (abs(diff_y) > 0.0001f)
	{
		if (abs(diff_x / diff_y) > 1.0f)
		{
			poly->orient = X_ORIENT;
			poly->slope = diff_y / diff_x;
			poly->bias = y2 - (poly->slope) * x2;
		}
		else
		{
			poly->orient = Y_ORIENT;
			poly->slope = diff_x / diff_y;
			poly->bias = x2 - (poly->slope) * y2;
		}

		return;
	}
}

vector<Poly_1D> Group_closer_lines(vector<Vec4i> lines, float slope_toler, float bias_toler)
{
	int idx;

	vector<Poly_1D> polys_pre;
	vector<Poly_1D> polys_post;

	for (idx = 0; idx < lines.size(); idx++)
	{
		Poly_1D poly;
		Vec4i line = lines[idx];

		Poly_fit_one_order((float)line[0], (float)line[1], (float)line[2], (float)line[3], &poly);

		if (poly.orient != ERR_ORIENT)
		{
			polys_pre.push_back(poly);
		}
	}

#if DEBUG == 1
	printf("polys_pre:\n");
	for (idx = 0; idx < polys_pre.size(); idx++)
	{
		printf("%f, %f, %d.\n", polys_pre[idx].bias, polys_pre[idx].slope, polys_pre[idx].orient);
	}
	printf("\n");
#endif // DEBUG == 1

	while (!polys_pre.empty())
	{
		vector<int> remove_list;
		Poly_1D tmp_poly1;
		Poly_1D poly_post;
		int count = 1;
		int size_remove_list = 0;

		tmp_poly1 = polys_pre.back();
		polys_pre.pop_back();

		poly_post.bias   = tmp_poly1.bias;
		poly_post.slope  = tmp_poly1.slope;
		poly_post.orient = tmp_poly1.orient;

		for (idx = 0; idx < polys_pre.size(); idx++)
		{
			if (polys_pre[idx].orient == tmp_poly1.orient)
			{
				if ((slope_toler > abs(polys_pre[idx].slope - tmp_poly1.slope)) & (bias_toler > abs(polys_pre[idx].bias - tmp_poly1.bias)))
				{
					poly_post.bias  += polys_pre[idx].bias;
					poly_post.slope += polys_pre[idx].slope;
					count++;

					remove_list.push_back(idx);
				}
			}
		}

		poly_post.bias  = poly_post.bias / count;
		poly_post.slope = poly_post.slope / count;
		polys_post.push_back(poly_post);

		size_remove_list = (int)remove_list.size();

		for (idx = 0; idx < size_remove_list; idx++)
		{
			int rm_idx = remove_list.back();

			remove_list.pop_back();
			polys_pre.erase(polys_pre.begin() + rm_idx);
		}
	}

#if DEBUG == 1
	printf("polys_post:\n");
	for (idx = 0; idx < polys_post.size(); idx++)
	{
		printf("%f, %f, %d.\n", polys_post[idx].bias, polys_post[idx].slope, polys_post[idx].orient);
	}
	printf("\n");
#endif // DEBUG == 1

	return polys_post;
}

Mat Draw_line(Mat input_img, vector<Poly_1D> polys, int img_width, int img_height)
{
	float width  = (float)img_width;
	float hieght = (float)img_height;
	int idx;

	for (idx = 0; idx < polys.size(); idx++)
	{
		vector<Vec2i> line_points;
		Vec2i line_point;
		float tmp_x1, tmp_x2, tmp_y1, tmp_y2;
		Poly_1D poly = polys[idx];

		if (poly.orient == X_ORIENT)
		{
			tmp_y1 = poly.bias;                          // x = 0
			tmp_y2 = poly.slope * width + poly.bias;     // x = width

			tmp_x1 = -1 * poly.bias / poly.slope;        // y = 0
			tmp_x2 = (hieght - poly.bias) / poly.slope;  // y = hieght
		}
		else // poly.orient == Y_ORIENT
		{
			tmp_x1 = poly.bias;                         // y = 0
			tmp_x2 = poly.slope * hieght + poly.bias;   // y = hieght

			tmp_y1 = -1 * poly.bias / poly.slope;       // x = 0
			tmp_y2 = (width - poly.bias) / poly.slope;  // x = width
		}

		if ((tmp_y1 > 0) & (tmp_y1 < hieght))
		{
			line_point[0] = 0;
			line_point[1] = (int)tmp_y1;

			line_points.push_back(line_point);
		}

		if ((tmp_y2 > 0) & (tmp_y2 < hieght))
		{
			line_point[0] = (int)width;
			line_point[1] = (int)tmp_y2;

			line_points.push_back(line_point);
		}

		if ((tmp_x1 > 0) & (tmp_x1 < width))
		{
			line_point[0] = (int)tmp_x1;
			line_point[1] = 0;

			line_points.push_back(line_point);
		}

		if ((tmp_x2 > 0) & (tmp_x2 < width))
		{
			line_point[0] = (int)tmp_x2;
			line_point[1] = (int)hieght;

			line_points.push_back(line_point);
		}

		line(input_img, Point(line_points[0][0], line_points[0][1]), Point(line_points[1][0], line_points[1][1]), Scalar(255, 0, 0), 2);
	}

	return input_img;
}

Vec2i Calculate_cross_point(Poly_1D poly1, Poly_1D poly2)
{
	Vec2i point;

	if (poly1.orient != poly2.orient)
	{
		if (poly1.orient == X_ORIENT)
		{
			point[0] = (int)((poly2.slope * poly1.bias + poly2.bias) / (1 - poly2.slope * poly1.slope)); // x = (m2 * b1 + b2) / (1 - m2 * m1)
			point[1] = (int)(poly1.slope * point[0] + poly1.bias); // y = m1 * x + b1
		}
		else // poly1.orient == Y_ORIENT
		{
			point[0] = (int)((poly1.slope * poly2.bias + poly1.bias) / (1 - poly1.slope * poly2.slope)); // x = (m1 * b2 + b1) / (1 - m1 * m2)
			//point[1] = (int)((point[0] - poly1.bias) / poly1.slope); // y = (x - b1) / m1
			point[1] = (int)(poly2.slope * point[0] + poly2.bias); // y = m2 * x + b2
		}
	}
	else
	{
		if (poly1.orient == X_ORIENT)
		{
			point[0] = (int)(-1 * (poly1.bias - poly2.bias) / (poly1.slope - poly2.slope)); // x = (b1 - b2) / (m1 - m2)
			point[1] = (int)(poly1.slope * point[0] + poly1.bias); // y = m1 * x + b1
		}
		else // poly1.orient == Y_ORIENT
		{
			point[1] = (int)(-1 * (poly1.bias - poly2.bias) / (poly1.slope - poly2.slope)); // y = (b1 - b2) / (m1 - m2)
			point[0] = (int)(poly1.slope * point[1] + poly1.bias); // x = m1 * y + b1
		}
	}

	return point;
}

vector<Vec2i> Extract_cross_points(vector<Poly_1D> polys, int img_width, int img_height)
{
	vector<Vec2i> points;
	int idx1;
	int idx2;
	int poly_size = (int)polys.size();

	for (idx1 = 0; idx1 < (poly_size - 1); idx1++)
	{
		for (idx2 = (idx1 + 1); idx2 < poly_size; idx2++)
		{
			Vec2i point = Calculate_cross_point(polys[idx1], polys[idx2]);
			int point_x = point[0];
			int point_y = point[1];

			if ((point_x > 0) & (point_x < img_width) & (point_y > 0) & (point_y < img_height))
			{
				points.push_back(point);
			}
		}
	}

	return points;
}

vector<Vec2i> Bubble_sort_for_axis(vector<Vec2i> in_points, int axis)
{
	int idx1, idx2;
	int num_points = (int)in_points.size();
	vector<Vec2i> out_points(num_points);

	copy(in_points.begin(), in_points.end(), out_points.begin());

#if DEBUG == 1
	printf("Before sort:\n");
	for (idx1 = 0; idx1 < in_points.size(); idx1++)
	{
		printf("Point:%d, %d.\n", in_points[idx1][0], in_points[idx1][1]);
	}
	printf("\n");
#endif // DEBUG == 1

	for (idx1 = 0; idx1 < num_points; idx1++)
	{
		for (idx2 = idx1 + 1; idx2 < num_points; idx2++)
		{
			if (out_points[idx1][axis] > out_points[idx2][axis])
			{
				swap(out_points[idx1], out_points[idx2]);
			}
		}
	}

#if DEBUG == 1
	printf("After sort:\n");
	for (idx1 = 0; idx1 < out_points.size(); idx1++)
	{
		printf("Point:%d, %d.\n", out_points[idx1][0], out_points[idx1][1]);
	}
	printf("\n");
#endif // DEBUG == 1

	return out_points;
}

vector<Vec2i> Find_points_close_to_camera_for_axis_with_range(vector<Vec2i> in_points, int range, int axis)
{
	vector<Vec2i> out_points;
	vector<Vec2i> tmp_points;
	Vec2i biggest_point;
	int num_points = (int)in_points.size();
	int idx;

	if (num_points == 0)
	{
		return out_points;
	}

	// Do bubble sort low to high
	tmp_points = Bubble_sort_for_axis(in_points, axis);
	biggest_point = tmp_points.back();
	tmp_points.pop_back();

	out_points.push_back(biggest_point);

	for (idx = num_points - 2; idx >= 0; idx--)  // "-2" because the biggest point had popped out. 
	{
		if (abs(biggest_point[axis] - tmp_points[idx][axis]) < range)
		{
			out_points.push_back(tmp_points[idx]);
		}
		else
		{
			goto END;
		}
	}

END:
#if DEBUG == 1
	printf("closer points:\n");
	for (idx = 0; idx < out_points.size(); idx++)
	{
		printf("Point:%d, %d.\n", out_points[idx][0], out_points[idx][1]);
	}
	printf("\n");
#endif // DEBUG == 1

	return out_points;
}

vector<Poly_1D> Filtrate_lines_for_axis(vector<Poly_1D> in_polys, int axis)
{
	vector<Poly_1D> out_polys;
	int num_poly = (int)in_polys.size();
	int idx;

	for (idx = 0; idx < num_poly; idx++)
	{
		if (axis == in_polys[idx].orient)
		{
			out_polys.push_back(in_polys[idx]);
		}
	}

	return out_polys;
}

float Get_parking_space_length(vector<Vec2i> points, float w_len_per_pix, float h_len_per_pix, int orientation)
{
	int idx1;
	int idx2;
	int num_points = (int)points.size();
	float ret_len = 0;

	for (idx1 = 0; idx1 < (num_points - 1); idx1++)
	{
		for (idx2 = (idx1 + 1); idx2 < num_points; idx2++)
		{
			float tmp_distance = 0;

			if (orientation == Y_ORIENT) // Distance for x axis
			{
				tmp_distance = (float)abs(points[idx1][0] - points[idx2][0]);
			}
			else if (orientation == X_ORIENT) // Distance for y axis
			{
				tmp_distance = (float)abs(points[idx1][1] - points[idx2][1]);
			}

			if (tmp_distance > ret_len)
			{
				ret_len = tmp_distance;
			}
		}
	}

	if (orientation == Y_ORIENT) // Distance for x axis
	{
		// - PARKING_LINE_WIDTH because we assume it will find the position at the middle of line width.
		ret_len = ret_len * w_len_per_pix - PARKING_LINE_WIDTH; // pixel to length (mm)
	}
	else if (orientation == X_ORIENT) // Distance for y axis
	{
		// - PARKING_LINE_WIDTH because we assume it will find the position at the middle of line width.
		ret_len = ret_len * h_len_per_pix - PARKING_LINE_WIDTH; // pixel to length (mm)
	}

	return ret_len;
}

float Get_distance_between_camera_and_points(vector<Vec2i> points, Size img_size, float w_len_per_pix, float h_len_per_pix, int orientation)
{
	float ret_len   = 0;
	float avg_value = 0;
	int idx;
	int num_points  = (int)points.size();

	for (idx = 0; idx < num_points; idx++)
	{
		if (orientation == Y_ORIENT) // Distance for y axis
		{
			avg_value += (float)points[idx][1];
		}
		else if (orientation == X_ORIENT) // Distance for x axis
		{
			avg_value += (float)points[idx][0];
		}
	}

	avg_value = avg_value / (float)num_points;

	if (orientation == Y_ORIENT) // Distance for y axis
	{
		avg_value = (float)img_size.height - avg_value;

		// - 0.5 * PARKING_LINE_WIDTH because we assume it will find the position at the middle of line width.
		ret_len = avg_value * w_len_per_pix - 0.5f * PARKING_LINE_WIDTH; // Pixel to length (mm)
	}
	else if (orientation == X_ORIENT) // Distance for x axis
	{
		avg_value = (float)img_size.width - avg_value;

		// - 0.5 * PARKING_LINE_WIDTH because we assume it will find the position at the middle of line width.
		ret_len = avg_value * h_len_per_pix - 0.5f * PARKING_LINE_WIDTH; // Pixel to length (mm)
	}

	return ret_len;
}

//=====================
// Event Trigger
//=====================
void on_mouse(int event, int x, int y, int flags, void* ustc)
{
	Mouse_event_prm *prm = (Mouse_event_prm*)ustc;

	if (is_object_selected == true)
	{
		select_region.x      = MIN(x, start_point.x);
		select_region.y      = MIN(y, start_point.y);
		select_region.width  = abs(x - start_point.x);
		select_region.height = abs(y - start_point.y);

		select_region &= Rect(0, 0, prm->src_img.cols, prm->src_img.rows);
	}

	switch ( event )
	{
	case CV_EVENT_LBUTTONDOWN:
		start_point        = Point(x, y);
		select_region      = Rect(x, y, 0, 0);
		is_object_selected = true;

		break;

	case CV_EVENT_LBUTTONUP:
	{
		float parking_space_len;
		float distance_cam_points;

		is_object_selected = false;
		selected_times++;

		width_len_per_pix  = REAL_BLOCK_WIDTH / (float)select_region.width;
		height_len_per_pix = REAL_BLOCK_HEIGHT / (float)select_region.height;

		parking_space_len   = Get_parking_space_length(prm->src_points, width_len_per_pix, height_len_per_pix, prm->orientation) / 1000;
		distance_cam_points = Get_distance_between_camera_and_points(prm->src_points, Size(prm->src_img.cols, prm->src_img.rows), width_len_per_pix, height_len_per_pix, prm->orientation) / 1000;

#if DEBUG == 1
		printf("select_region, x:%d, y:%d, width:%d, height:%d\n", select_region.x, select_region.y, select_region.width, select_region.height);
		printf("length per pixel w:%f, h:%f\n", width_len_per_pix, height_len_per_pix);
		printf("length of parking lot:%f meter\n", parking_space_len);
		printf("distance between camera and points:%f meter\n", distance_cam_points);
		printf("\n");
#endif
		break;
	}
	default:
		break;
	}
}

//=====================
// APA Pipe Line
//=====================
Mat Recognizing_parking_lot(Mat in_gray_img, vector<Poly_1D> *out_polys, vector<Vec2i> *out_points)
{

}

//=====================
// Main
//=====================
int main()
{
	int orientation  = Y_ORIENT;

	String file_name = "img4.jpeg";
	Size img_size    = Size(504, 378);

	Mat img              = imread(file_name);
	Mat resized_img      = Mat(img_size, CV_32S);
	Mat wrap_resized_img = Mat(img_size, CV_32S);
	Mat gray             = Mat(img_size, CV_32S);
	Mat bin_img;
	Mat pers_img;
	Mat edge;
	Mat group_line_img;
	Mat final_img        = Mat(img_size, CV_32S);

	vector<Vec4i> lines;
	vector<Poly_1D> polys;
	vector<Poly_1D> final_polys;
	vector<Vec2i> cross_points;
	vector<Vec2i> closer_points;

	Mouse_event_prm mouse_event_prm;

	//Get Perspective Transform Matrix
	Mat M_pers = getPerspectiveTransform(src_points, dst_points);

	printf("image size w:%d, h:%d\n\n", img_size.width, img_size.height);

	resize(img, resized_img, img_size);

	imshow("original image:", img);
	imshow("resized image:", resized_img);

	cvtColor(resized_img, gray, CV_BGR2GRAY);

	imshow("gray image:", gray);

	bin_img = Binarization(gray, Size(KERNEL_SIZE, KERNEL_SIZE), BINARY_THRESHOLD);

	imshow("binary image:", bin_img);

	warpPerspective(bin_img, pers_img, M_pers, Size(bin_img.cols, bin_img.rows), INTER_LINEAR);
	warpPerspective(resized_img, wrap_resized_img, M_pers, Size(bin_img.cols, bin_img.rows), INTER_LINEAR);
	warpPerspective(resized_img, final_img, M_pers, Size(bin_img.cols, bin_img.rows), INTER_LINEAR);

	imshow("perspective image:", pers_img);

	edge = Canny_Process(pers_img, CANNY_LOW_THRLD, CANNY_HIGH_THRLD, KERNEL_SIZE);

	imshow("edge image:", edge);

	HoughLinesP(edge, lines, 1, CV_PI / 180, 45, 45, 10);

	for (size_t i = 0; i < lines.size(); i++)
	{
		Vec4i l = lines[i];
		line(wrap_resized_img, Point(l[0], l[1]), Point(l[2], l[3]), Scalar(0, 0, 255), 2);
	}

	imshow("hough image:", wrap_resized_img);

	polys = Group_closer_lines(lines, 0.2f, 35.0f);   // Those parameter should be change at differnce resolution.

	group_line_img = Draw_line(wrap_resized_img, polys, img_size.width, img_size.height);

	imshow("group line image:", wrap_resized_img);

	cross_points = Extract_cross_points(polys, img_size.width, img_size.height);

	for (size_t i = 0; i < cross_points.size(); i++)
	{
		Vec2i l = cross_points[i];
		circle(wrap_resized_img, Point(l[0], l[1]), 10, Scalar(0, 255, 0), 2);
	}

	imshow("group line image:", wrap_resized_img);

	closer_points = Find_points_close_to_camera_for_axis_with_range(cross_points, 48, orientation);

	for (size_t i = 0; i < closer_points.size(); i++)
	{
		Vec2i l = closer_points[i];
		circle(final_img, Point(l[0], l[1]), 10, Scalar(0, 255, 0), 2);
	}

	final_polys = Filtrate_lines_for_axis(polys, orientation);

	final_img = Draw_line(final_img, final_polys, img_size.width, img_size.height);

	imshow("final_img", final_img);

	mouse_event_prm.orientation = orientation;
	mouse_event_prm.src_img     = final_img;
	mouse_event_prm.src_points  = closer_points;

	setMouseCallback("final_img", on_mouse, &mouse_event_prm);

	waitKey(1000000);
	return 0;
}

