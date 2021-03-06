#include "stdafx.h"
#include "ImageLoc.h"
#include "helpfunc.h"
#include <time.h>
#include <numeric>
using std::to_wstring;
//检查是否为透明图，返回透明像素个数
int check_transparent(Image* img) {
	if (img->width < 2 || img->height < 2)
		return 0;
	uint c0 = *img->begin();
	bool x = c0 == img->at<uint>(0, img->width - 1) &&
		c0 == img->at<uint>(img->height - 1, 0) &&
		c0 == img->at<uint>(img->height - 1, img->width - 1);
	if (!x)
		return 0;

	int ct = 0;
	for (auto it : *img)
		if (it == c0)
			++ct;

	return ct < img->height*img->width ? ct : 0;
}

void get_match_points(const Image& img, vector<uint>&points) {
	points.clear();
	uint cbk = *img.begin();
	for (int i = 0; i < img.height; ++i) {
		for (int j = 0; j < img.width; ++j)
			if (cbk != img.at<uint>(i, j))
				points.push_back((i << 16) | j);
	}
}

void gen_next(const Image& img, vector<int>& next) {
	next.resize(img.width*img.height);

	auto t = img.ptr<int>(0);
	auto p = next.data();
	p[0] = -1;
	int k = -1, j = 0;
	while (j < next.size() - 1) {
		if (k == -1 || t[k] == t[j]) {
			k++;
			j++;
			p[j] = k;
		}
		else {
			k = p[k];
		}
	}
}


ImageBase::ImageBase()
{
	_x1 = _y1 = 0;
	_dx = _dy = 0;
}


ImageBase::~ImageBase()
{
}

long ImageBase::input_image(byte* psrc, int width, int height, long x1, long y1, long x2, long y2, int type) {
	int i, j;
	_x1 = x1; _y1 = y1;
	int cw = x2 - x1, ch = y2 - y1;
	_src.create(cw, ch);
	if (type == -1) {//倒过来读
		uchar *p, *p2;
		for (i = 0; i < ch; ++i) {
			p = _src.ptr<uchar>(i);
			p2 = psrc + (height - i - 1 - y1) * width * 4 + x1 * 4;//偏移
			memcpy(p, p2, 4 * cw);
		}
	}
	else {
		uchar *p, *p2;
		for (i = 0; i < ch; ++i) {
			p = _src.ptr<uchar>(i);
			p2 = psrc + (i + y1) * width * 4 + x1 * 4;
			memcpy(p, p2, 4 * cw);
		}
	}
	return 1;
}

void ImageBase::set_offset(int dx, int dy) {
	_dx = -dx;
	_dy = -dy;
}

template<bool nodfcolor>
long ImageBase::simple_match(long x, long y, Image* timg, color_t dfcolor, int max_error) {
	int err_ct = 0;


	uint* pscreen_top, *pscreen_bottom, *pimg_top, *pimg_bottom;
	pscreen_top = _src.ptr<uint>(y) + x;
	pscreen_bottom = _src.ptr<uint>(y + timg->height - 1) + x;
	pimg_top = timg->ptr<uint>(0);
	pimg_bottom = timg->ptr<uint>(timg->height - 1);
	while (pscreen_top <= pscreen_bottom) {

		auto ps1 = pscreen_top, ps2 = pscreen_top + timg->width - 1;
		auto ps3 = pscreen_bottom, ps4 = pscreen_bottom + timg->width - 1;
		auto pt1 = pimg_top, pt2 = pimg_top + timg->width - 1;
		auto pt3 = pimg_bottom, pt4 = pimg_bottom + timg->width - 1;
		while (ps1 <= ps2) {
			if (nodfcolor) {
				if (*ps1++ != *pt1++)++err_ct;//top left
				if (*ps2-- != *pt2--)++err_ct;//top right
				if (*ps3++ != *pt3++)++err_ct;//bottom left
				if (*ps4-- != *pt4--)++err_ct;//bottom right
			}
			else {
				if (!IN_RANGE(*(color_t*)ps1++, *(color_t*)pt1++, dfcolor))
					++err_ct;
				if (!IN_RANGE(*(color_t*)ps2--, *(color_t*)pt2--, dfcolor))
					++err_ct;
				if (!IN_RANGE(*(color_t*)ps3++, *(color_t*)pt3++, dfcolor))
					++err_ct;
				if (!IN_RANGE(*(color_t*)ps4--, *(color_t*)pt4--, dfcolor))
					++err_ct;
			}

			if (err_ct > max_error)
				return 0;
		}
		pscreen_top += _src.width;
		pscreen_bottom -= _src.width;
	}

	return 1;
}
template<bool nodfcolor>
long ImageBase::trans_match(long x, long y, Image* timg, color_t dfcolor, vector<uint>pts, int max_error) {
	int err_ct = 0, k, dx, dy;
	int left, right;
	left = 0; right = pts.size() - 1;
	while (left <= right) {
		auto it = pts[left];
		if (nodfcolor) {
			if (_src.at<uint>(y + PTY(pts[left]), x + PTX(pts[left])) != timg->at<uint>(PTY(pts[left]), PTX(pts[left])))
				++err_ct;
			if (_src.at<uint>(y + PTY(pts[right]), x + PTX(pts[right])) != timg->at<uint>(PTY(pts[right]), PTX(pts[right])))
				++err_ct;
		}
		else {
			color_t cr1, cr2;
			cr1 = _src.at<color_t>(y + PTY(pts[left]), x + PTX(pts[left]));
			cr2 = timg->at<color_t>(PTY(pts[left]), PTX(pts[left]));
			if (!IN_RANGE(cr1, cr2, dfcolor))
				++err_ct;
			cr1 = _src.at<color_t>(y + PTY(pts[right]), x + PTX(pts[right]));
			cr2 = timg->at<color_t>(PTY(pts[right]), PTX(pts[right]));
			if (!IN_RANGE(cr1, cr2, dfcolor))
				++err_ct;
		}

		++left; --right;
		if (err_ct > max_error)
			return 0;
	}
	return 1;
}

long ImageBase::real_match(long x, long y, ImageBin* timg,int tnorm, double sim) {
	//quick check
	if ((double)abs(tnorm - region_sum(x, y, x + timg->width, y + timg->height)) / (double)tnorm > 1.0 - sim)
		return 0;
	int err = 0;
	for (int i = 0; i < timg->height; i++) {
		auto ptr = _gray.ptr(y + i) + x;
		auto ptr2 = timg->ptr(i);
		for (int j = 0; j < timg->width; j++) {
			err += abs(*ptr - *ptr2);
			ptr++; ptr2++;
		}
	}
	//norm it
	double nerr = (double)err / ((double)tnorm);
	return nerr <= 1.0 - sim ? 1 : 0;

}

void ImageBase::record_sum() {
	_sum.create(_src.width, _src.height);
	//_sum.fill(0);
	int m = _gray.height;
	int n = _gray.width;
	for (int i = 0; i < m; i++) {
		
		for (int j = 0; j < n; j++) {
			int &s = _sum.at<int>(i, j);
			if (i)
				 s+= _sum.at<int>(i - 1, j);
			if(j)
				s += _sum.at<int>(i, j-1);
			if (i&&j)
				s -= _sum.at<int>(i - 1, j - 1);
			s += (int)_gray.at(i, j);
		}
	}
}

int ImageBase::region_sum(int x1, int y1, int x2, int y2) {
	int ans = _sum.at<int>(y2 - 1, x2 - 1);
	if (x1)ans -= _sum.at<int>(y2-1, x1 - 1);
	if (y1)ans -= _sum.at<int>(y1 - 1, x2-1);
	if (x1&&y1)ans += _sum.at<int>(y1 - 1, x1 - 1);
	return ans;
}

long ImageBase::GetPixel(long x, long y, color_t&cr) {
	if (!is_valid(x, y)) {
		setlog("Invalid pos:%d %d", x, y);
		return 0;
	}

	auto p = _src.ptr<uchar>(y) + 4 * x;
	static_assert(sizeof(color_t) == 4);
	cr.b = p[0]; cr.g = p[1]; cr.r = p[2];
	return 1;
}

long ImageBase::CmpColor(long x, long y, std::vector<color_df_t>&colors, double sim) {
	color_t cr;
	if (GetPixel(x, y, cr)) {
		for (auto&it : colors) {
			if (IN_RANGE(cr, it.color, it.df))
				return 1;
		}
	}
	return 0;
}

long ImageBase::FindColor(vector<color_df_t>& colors, long&x, long&y) {
	for (int i = 0; i < _src.height; ++i) {
		auto p = _src.ptr<color_t>(i);
		for (int j = 0; j < _src.width; ++j) {
			for (auto&it : colors) {//对每个颜色描述
				if (IN_RANGE(*p, it.color, it.df)) {
					x = j + _x1 + _dx; y = i + _y1 + _dy;
					return 1;
				}
			}
			p++;
		}
	}
	x = y = -1;
	return 0;
}

long ImageBase::FindColorEx(vector<color_df_t>& colors, std::wstring& retstr) {
	retstr.clear();
	int find_ct = 0;
	for (int i = 0; i < _src.height; ++i) {
		auto p = _src.ptr<color_t>(i);
		for (int j = 0; j < _src.width; ++j) {
			for (auto&it : colors) {//对每个颜色描述
				if (IN_RANGE(*p, it.color, it.df)) {
					retstr += std::to_wstring(j + _x1 + _dx) + L"," + std::to_wstring(i + _y1 + _dy);
					retstr += L"|";
					++find_ct;
					//return 1;
					if (find_ct > _max_return_obj_ct)
						goto _quick_break;
					break;
				}
			}
			p++;
		}
	}
_quick_break:
	if (!retstr.empty() && retstr.back() == L'|')
		retstr.pop_back();
	return find_ct;
}

long ImageBase::FindMultiColor(std::vector<color_df_t>&first_color, std::vector<pt_cr_df_t>& offset_color, double sim, long dir, long&x, long&y) {
	int max_err_ct = offset_color.size()*(1. - sim);
	int err_ct;
	for (int i = 0; i < _src.height; ++i) {
		auto p = _src.ptr<color_t>(i);
		for (int j = 0; j < _src.width; ++j) {
			//step 1. find first color
			for (auto&it : first_color) {//对每个颜色描述
				if (IN_RANGE(*p, it.color, it.df)) {
					//匹配其他坐标
					err_ct = 0;
					for (auto&off_cr : offset_color) {
						if (!CmpColor(j + off_cr.x, i + off_cr.y, off_cr.crdfs, sim))
							++err_ct;
						if (err_ct > max_err_ct)
							goto _quick_break;
					}
					//ok
					x = j + _x1 + _dx, y = i + _y1 + _dy;
					return 1;
				}
			}
		_quick_break:
			p++;
		}
	}
	x = y = -1;
	return 0;
}

long ImageBase::FindMultiColorEx(std::vector<color_df_t>&first_color, std::vector<pt_cr_df_t>& offset_color, double sim, long dir, std::wstring& retstr) {
	int max_err_ct = offset_color.size()*(1. - sim);
	int err_ct;
	int find_ct = 0;
	for (int i = 0; i < _src.height; ++i) {
		auto p = _src.ptr<color_t>(i);
		for (int j = 0; j < _src.width; ++j) {
			//step 1. find first color
			for (auto&it : first_color) {//对每个颜色描述
				if (IN_RANGE(*p, it.color, it.df)) {
					//匹配其他坐标
					err_ct = 0;
					for (auto&off_cr : offset_color) {
						if (!CmpColor(j + off_cr.x, i + off_cr.y, off_cr.crdfs, sim))
							++err_ct;
						if (err_ct > max_err_ct)
							goto _quick_break;
					}
					//ok
					retstr += to_wstring(j + _x1 + _dx) + L"," + to_wstring(i + _y1 + _dy);
					retstr += L"|";
					++find_ct;
					if (find_ct > _max_return_obj_ct)
						goto _quick_return;
					else
						goto _quick_break;
				}
			}
		_quick_break:
			p++;
		}
	}
_quick_return:
	if (!retstr.empty() && retstr.back() == L'|')
		retstr.pop_back();
	return find_ct;
	//x = y = -1;
}

long ImageBase::FindPic(std::vector<Image*>&pics, color_t dfcolor, double sim, long&x, long&y) {
	//setlog("pic count=%d", pics.size());
	/*if (sim == 1.0)
		return FindPicKmp(pics, dfcolor, x, y);*/
	auto t1 = clock();
	x = y = -1;
	vector<uint> points;
	//bool nodfcolor = color2uint(dfcolor) == 0;
	int match_ret = 0;
	ImageBin gimg;
	_gray.fromImage4(_src);
	record_sum();
	int tnorm;
	//将小循环放在最外面，提高处理速度
	for (int pic_id = 0; pic_id < pics.size(); ++pic_id) {
		auto pic = pics[pic_id];
		int use_ts_match = check_transparent(pic);
		//setlog("use trans match=%d", use_ts_match);
		if (use_ts_match)
			get_match_points(*pic, points);
		else {
			gimg.fromImage4(*pic);
			tnorm = sum(gimg.begin(), gimg.end());
		}
			
		for (int i = 0; i < _src.height; ++i) {
			for (int j = 0; j < _src.width; ++j) {
				//step 1. 边界检查
				if (i + pic->height > _src.height || j + pic->width > _src.width)
					continue;
				//step 2. 计算最大误差
				int max_err_ct = (pic->height*pic->width - use_ts_match)*(1.0 - sim);
				//step 3. 开始匹配


				/*match_ret = (use_ts_match ? trans_match<false>(j, i, pic, dfcolor, points, max_err_ct) :
					simple_match<false>(j, i, pic, dfcolor, max_err_ct));*/
				match_ret = (use_ts_match ? trans_match<false>(j, i, pic, dfcolor, points, max_err_ct) :
					real_match(j, i, &gimg,tnorm, sim));
				if (match_ret) {
					x = j + _x1 + _dx; y = i + _y1 + _dy;
					return pic_id;
				}

			}//end for j
		}//end for i
	}//end for pics
	return -1;
}

long ImageBase::FindPicEx(std::vector<Image*>&pics, color_t dfcolor, double sim, wstring& retstr) {
	int obj_ct = 0;
	retstr.clear();
	vector<uint> points;
	bool nodfcolor = color2uint(dfcolor) == 0;
	int match_ret = 0;
	ImageBin gimg;
	_gray.fromImage4(_src);
	record_sum();
	int tnorm;
	for (int pic_id = 0; pic_id < pics.size(); ++pic_id) {
		auto pic = pics[pic_id];
		int use_ts_match = check_transparent(pic);

		if (use_ts_match)
			get_match_points(*pic, points);
		else {
			gimg.fromImage4(*pic);
			tnorm = sum(gimg.begin(), gimg.end());
		}
		for (int i = 0; i < _src.height; ++i) {
			for (int j = 0; j < _src.width; ++j) {

				//step 1. 边界检查
				if (i + pic->height > _src.height || j + pic->width > _src.width)
					continue;
				//step 2. 计算最大误差
				int max_err_ct = (pic->height*pic->width - use_ts_match)*(1.0 - sim);
				//step 3. 开始匹配
				/*if (nodfcolor)
					match_ret = (use_ts_match ? trans_match<true>(j, i, pic, dfcolor, points, max_err_ct) :
						simple_match<true>(j, i, pic, dfcolor, max_err_ct));
				else
					match_ret = (use_ts_match ? trans_match<false>(j, i, pic, dfcolor, points, max_err_ct) :
						simple_match<false>(j, i, pic, dfcolor, max_err_ct));*/
				match_ret = (use_ts_match ? trans_match<false>(j, i, pic, dfcolor, points, max_err_ct) :
					real_match(j, i, &gimg, tnorm, sim));
				if (match_ret) {
					retstr += std::to_wstring(j + _x1 + _dx) + L"," + std::to_wstring(i + _y1 + _dy);
					retstr += L"|";
					++obj_ct;
					if (obj_ct > _max_return_obj_ct)
						goto _quick_return;
					else
						break;
				}


			}//end for j
		}//end for i
	}//end for pics
_quick_return:
	return obj_ct;
}

long ImageBase::Ocr(Dict& dict, double sim, wstring& retstr) {
	retstr.clear();
	std::map<point_t, wstring> ps;
	bin_ocr(_binary, _record, dict, sim, ps);
	for (auto&it : ps) {
		retstr += it.second;
	}
	return 1;
}

long ImageBase::OcrEx(Dict& dict, double sim, std::wstring& retstr) {
	retstr.clear();
	std::map<point_t, wstring> ps;
	bin_ocr(_binary, _record, dict, sim, ps);
	//x1,y1,str....|x2,y2,str2...|...
	int find_ct = 0;
	for (auto&it : ps) {
		retstr += std::to_wstring(it.first.x + _x1 + _dx);
		retstr += L",";
		retstr += std::to_wstring(it.first.y + _y1 + _dy);
		retstr += L",";
		retstr += it.second;
		retstr += L"|";
		++find_ct;
		if (find_ct > _max_return_obj_ct)
			break;
	}
	if (!retstr.empty() && retstr.back() == L'|')
		retstr.pop_back();
	return find_ct;
}

long ImageBase::FindStr(Dict& dict, const vector<wstring>& vstr, double sim, long& retx, long& rety) {
	retx = rety = -1;
	std::map<point_t, wstring> ps;
	bin_ocr(_binary, _record, dict, sim, ps);
	for (auto&it : ps) {
		for (auto&s : vstr) {
			if (it.second == s) {
				retx = it.first.x + _x1 + _dx;
				rety = it.first.y + _y1 + _dy;
				return 1;
			}
		}
	}
	return 0;
}

long ImageBase::FindStrEx(Dict& dict, const vector<wstring>& vstr, double sim, std::wstring& retstr) {
	retstr.clear();
	std::map<point_t, wstring> ps;
	bin_ocr(_binary, _record, dict, sim, ps);
	int find_ct = 0;
	for (auto&it : ps) {
		for (auto&s : vstr) {
			if (it.second == s) {
				retstr += std::to_wstring(it.first.x + _x1 + _dx);
				retstr += L",";
				retstr += std::to_wstring(it.first.y + _y1 + _dy);
				retstr += L"|";
				++find_ct;
				if (find_ct > _max_return_obj_ct)
					goto _quick_return;
				else
					break;
			}
		}
	}
_quick_return:
	if (!retstr.empty() && retstr.back() == L'|')
		retstr.pop_back();
	return find_ct;
}
