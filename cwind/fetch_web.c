#include "cwind.h"
#include "comfunc.h"
#include "event.h"
#include "global.h"

extern cw_config_t server;

int fetch_swf_url(cw_content_t * content);

void content_free(cw_content_t * content)
{
	if (!content)
		return;

	string_free(&content->desc);
	string_free(&content->caption);
	string_free(&content->content);
}

int check_update(cw_site_t * site, html_page_t * page)
{
	string_t content;
	char md5[MAX_MD5];

	if (string_fetch(&page->data, site->hot_part_begin, site->hot_part_end, &content) == MRT_ERR) {
		log_error("no found hot part start:%s, end:%s, url:%s",
			  site->hot_part_begin, site->hot_part_end, site->url);
		return MRT_ERR;
	}

	if (make_md5((unsigned char *)content.str, content.len, md5)) {
		log_error("make md5 sitering error, will recv this site, site:[%s], url:[%s].", site->name, site->url);
		return MRT_SUC;
	}

	if (strcmp(md5, site->md5) == 0) {
		log_info("same md5 sitering, will stop recv current site:[%s], url:[%s].", site->name, site->url);
		return MRT_ERR;
	}

	memset(site->md5, 0, MAX_MD5);

	strcpy(site->md5, md5);

	//printf("new md5:%s\n", md5);

	return MRT_OK;
}

hashmap_t urlMap = { 0 };

//返回1存在
int url_cache_find(char *url)
{
	char *val = NULL;

	if (!urlMap.size)
		return MRT_OK;

	if ((val = hashmap_find(&urlMap, url, strlen(url)))) {
		log_debug("url:%s status:%s", url, val);
		return MRT_ERR;
	}

	return MRT_OK;
}

int url_cache_add(char *url)
{
	if (!urlMap.size) {
		if (hashmap_init(&urlMap, 12345) == MRT_ERR) {
			log_error("hashmap_init error:%s", get_error());
			return MRT_ERR;
		}
	}

	if (hashmap_insert(&urlMap, url, strlen(url), "1")) {
		log_debug("insert url:%s", url);
		return MRT_OK;
	}

	return MRT_ERR;
}

int fetch_next_page(cw_site_t * site, html_page_t * page, string_t * url)
{
	cw_list_tag_t *ltag = site->list_tag;
	string_t part;

	s_zero(part);

	if (string_fetch(&page->data, ltag->page_part_begin, ltag->page_part_end, &part) == MRT_ERR) {
		log_error("No found page part error:%s.", get_error());
		return MRT_ERR;
	}

	while (!html_move_fetch_href(&part, url, NULL)) {
		/*
		   if(strcmp(caption, ltag->next_caption) == 0)
		   {
		   html_fix_url(page, url);
		   return MRT_OK;
		   }
		 */
		html_fix_url(page, url);
		if (url_cache_find(url->str) == MRT_OK) {
			log_debug("page url:%s next url:%s", page->url, url->str);
			url_cache_add(url->str);
			string_free(&part);
			return MRT_SUC;
		}
	}

	string_free(&part);
	log_error("No found next page url:%s.", page->url);

	return MRT_ERR;
}

//功能：
//      把当前页面的url收集下, 取ltag->caption_begin与ltag->caption_end之间的url，一直找到没有
//
int fetch_article_url(cw_site_t * site, cw_class_t * class, html_page_t * page)
{
	cw_content_t *content = NULL;
	cw_list_tag_t *ltag = site->list_tag;
	string_t list, part, item, url, caption;

	s_zero(list);
	s_zero(part);
	s_zero(url);
	s_zero(caption);
	s_zero(item);

	if (string_move_fetch(&page->data, ltag->list_part_begin, ltag->list_part_end, &list) == MRT_ERR) {
		log_error("string_move_fetch list error:%s", get_error());
		return MRT_ERR;
	}

	while (string_move_fetch(&list, ltag->href_begin, ltag->href_end, &part)
	       == MRT_SUC) {
		if (*ltag->caption_begin && *ltag->caption_end) {
			if (string_fetch(&part, ltag->caption_begin, ltag->caption_end, &item) == MRT_ERR) {
				log_error("find href error:%s, part:%s", get_error(), part.str);
				continue;
			}

			if (html_fetch_href(&item, &url, &caption) == MRT_ERR) {
				log_error("no found url, item:%s", item.str);
				continue;
			}
		} else {
			if (html_fetch_href(&part, &url, &caption) == MRT_ERR) {
				log_error("no found url, part:%s", part.str);
				continue;
			}
		}

		if (html_fix_url(page, &url) == MRT_ERR) {
			log_error("html_fix_url error, url:%s", url.str);
			continue;
		}

		if (find_url_status(site, url.str, 2) == 1) {
			log_info("url:%s has been collected", url.str);
			continue;
		}

		if (!(content = M_alloc(sizeof(*content)))) {
			log_error("malloc new content error.");
			continue;
		}

		if (content->first_class.len) {
			if (string_copy(&content->first_class, &class->first_class) == MRT_ERR) {
				log_error("string_copy first_class error:%s", get_error());
				//    continue;
			}
		}

		if (string_copy(&content->second_class, &class->second_class) == MRT_ERR) {
			log_error("string_copy second_class error:%s", get_error());
			continue;
		}

		if (string_copy(&content->url_real, &url) == MRT_ERR) {
			log_error("string_copy url_preview error:%s", get_error());
			continue;
		}

		LIST_INSERT_HEAD(site->content_list, head, content, node);

		log_debug("find clase first:%s second:%s caption:%s url:%s",
			  content->first_class.str, content->second_class.str, caption.str, content->url_real.str);
	}

	string_free(&list);
	string_free(&part);
	string_free(&url);
	string_free(&caption);
	string_free(&item);

	return MRT_SUC;
}

/*
int fetch_joke_url(cw_site_t *site, html_page_t *page, char *part)
{
    cw_content_t *content = NULL;
    int gnum = 0;
    char url[MAX_URL] = {0};
    char caption[MAX_CAPTION] = {0};
    cw_list_tag_t *ltag = site->list_tag;

    for(;part && (part = str_jump_tag(part, ltag->href_part_begin)); (part = str_jump_tag(part, ltag->href_part_end)))
    {
        M_cicl(comm_gets(part, ltag->caption_begin, ltag->caption_end, caption, sizeof(caption)),
               "no found caption:%s", part);
        M_cicl(comm_gets(part, ltag->href_begin, ltag->href_end, url, sizeof(url)),
               "no found url:%s", part);

        //printf("start:%s,end:%s, part:%s\n",  ltag->href_begin, ltag->href_end, part);
        M_cvril((content = M_alloc(sizeof(*content))), "malloc new content error.");
        strcpy(content->url_real.str, url);
        html_fix_url(page, content->url_real.str);

        //printf("url:%s\n", content->url_real.str);

        M_list_insert_head(site->content_list, content);
        s_zero(url);
        gnum++;
    }

    return gnum;

}
*/

int fix_content_image(html_page_t * page, string_t * content)
{
	string_t simg, url, hurl, limg;
	int len;

	s_zero(simg);
	s_zero(url);
	s_zero(hurl);
	s_zero(limg);

	while ((string_move_fetch(content, "<img", ">", &simg) == MRT_SUC)
	       || (string_move_fetch(content, "<IMG", ">", &simg) == MRT_SUC)) {
		len = content->idx - content->str - simg.len;
		if (string_fetch(&simg, "src=\"", "\"", &url) == MRT_ERR) {
			log_error("no found image 'src', error:%s", get_error());
			string_free(&simg);
			return MRT_ERR;
		}
		string_free(&simg);

		if (string_copy(&hurl, &url) == MRT_ERR) {
			log_error("string_copy hurl error:%s", get_error());
			string_free(&url);
			return MRT_ERR;
		}

		html_fix_url(page, &url);

		if (http_recv_file(url.str, page->url, &limg) == MRT_ERR) {
			log_error("http recv file error, url:%s", url.str);
			string_free(&url);
			string_free(&hurl);
			return MRT_ERR;
		}
		string_free(&url);

		if (string_replace(content, hurl.str, limg.str) == MRT_ERR) {
			log_error("string_replace error:%s", get_error());
			string_free(&hurl);
			string_free(&limg);
			return MRT_ERR;
		}

		content->idx = content->str + len;
		log_info("replace img src, from:%s to:%s", hurl.str, limg.str);
	}

	string_free(&hurl);
	string_free(&limg);

	return MRT_SUC;
}

int fix_caption(string_t * cp)
{
	char *pl = NULL, *pr = NULL;

	M_cpvril(cp);

	while ((pl = strchr(cp->str, '<'))) {
		if (!(pr = strchr(pl, '>')))
			break;

		memmove(pl, pr + 1, cp->len - (pr - cp->str) - 1);
		cp->len -= pr - pl;
		cp->str[cp->len] = 0;
	}

	return MRT_SUC;
}

int fetch_article_content(cw_site_t * site)
{
	cw_content_tag_t *ctag = NULL;
	cw_content_t *content = NULL;
	html_page_t page;
	string_t part, desc;

	ctag = site->content_tag;

	s_zero(page);
	s_zero(part);
	s_zero(desc);

	while ((content = LIST_FIRST(site->content_list, head))) {
		do {
			log_debug("will recv page url:%s", content->url_real.str);
			if (http_recv_page(content->url_real.str, &page, 1) == MRT_ERR) {
				log_error("can't get page url:%s", content->url_real.str);
				break;
			}

			if (html_fix_charset(&page, "UTF-8") == MRT_ERR) {
				log_error("change page charset to UTF-8 error, url:%s", content->url_real.str);
				break;
			}

			if (string_fetch(&page.data, ctag->part_begin, ctag->part_end, &part) == MRT_ERR) {
				log_error("string_fetch part error:%s, url:%s", get_error(), content->url_real.str);
				break;
			}

			if (string_fetch(&part, ctag->caption_begin, ctag->caption_end, &content->caption) == MRT_ERR) {
				log_error
				    ("string_fetch caption error:%s url:%s, src:%s",
				     get_error(), content->url_real.str, part.str);
				break;
			}

			fix_caption(&content->caption);

			if (string_fetch(&part, ctag->desc_begin, ctag->desc_end, &content->desc) == MRT_ERR) {
				log_error
				    ("No found desc part in url:%s, part start:%s, end:%s.",
				     content->url_real.str, ctag->desc_begin, ctag->desc_end);
			}

			if (string_fetch(&part, ctag->content_begin, ctag->content_end, &content->content) == MRT_ERR) {
				log_error
				    ("string_fetch content error:%s url:%s, part start:%s, end:%s.",
				     get_error(), content->url_real.str, ctag->content_begin, ctag->content_end);
				break;
			}
//            log_debug("will fix content image, url:%s", content->url_real.str);
			if (fix_content_image(&page, &content->content) == MRT_ERR) {
				log_error("fix_content_image error, url:[%s].", content->url_real.str);
				break;
			}

			if (html_mark_filter(site->filter, &content->content) == MRT_ERR) {
				log_error("html_mark_filter content error, url:[%s].", content->url_real.str);
				break;
			}

			if (content_info_save(site, content) == MRT_ERR) {
				log_error("save current joke error, url:[%s].", content->url_real.str);
				set_url_status(site, content->url_real.str, 2, 2);
			} else {
				set_url_status(site, content->url_real.str, 2, 1);
				log_info("save success url:[%s]", content->url_real.str);
			}
		} while (0);

		LIST_REMOVE_HEAD(site->content_list, head);
		content_free(content);
		http_free_page(&page);
	}

	string_free(&part);
	string_free(&desc);

	return MRT_SUC;
}

int fix_class_info(cw_site_t * site, html_page_t * page)
{
	cw_class_t *class = NULL;
	char *pstr = NULL;

	LIST_FOREACH(class, node, site->class_list, head) {
		html_fix_url(page, &class->url);
		html_fix_caption(page, &class->second_class);
		if ((pstr = strchr(class->second_class.str, '('))) {
			*pstr = 0;
			class->second_class.len = strlen(class->second_class.str);
		}
		log_info("Current class caption:%s, url:%s", class->second_class.str, class->url.str);
	}

	return MRT_SUC;
}

int fetch_class(cw_site_t * site, html_page_t * page)
{
	cw_class_t *class = NULL;
	cw_class_tag_t *ct = site->class_tag;
	string_t part, url, caption;
	char *pstr = NULL;

	s_zero(part);
	s_zero(url);
	s_zero(caption);

	if (string_move_fetch(&page->data, ct->part_begin, ct->part_end, &part)
	    == MRT_ERR) {
		log_error("no found class part start:%s, end:%s, in src:%s.",
			  ct->part_begin, ct->part_end, page->data.str);
		return MRT_ERR;
	}

	while (html_move_fetch_href(&part, &url, &caption) == MRT_SUC) {
		M_cvril((class = M_alloc(sizeof(*class))), "malloc new class error.");

		if (html_fix_url(page, &url) == MRT_ERR) {
			log_error("html_fix_url error, url:%s", url.str);
			continue;
		}

		if (html_fix_caption(page, &caption) == MRT_ERR) {
			log_error("html_fix_caption error, caption:%s", caption.str);
			continue;
		}
		//分类里的标题要过滤掉()[], 这里面可能写的是数量
		if ((pstr = strchr(caption.str, '('))) {
			*pstr = 0;
			caption.len = strlen(caption.str);
		}

		if ((pstr = strchr(caption.str, '['))) {
			*pstr = 0;
			caption.len = strlen(caption.str);
		}

		if (string_copy(&class->url, &url) == MRT_ERR) {
			log_error("string_copy url error:%s", get_error());
			continue;
		}

		if (string_copy(&class->second_class, &caption) == MRT_ERR) {
			log_error("string_copy second_class error:%s", get_error());
			continue;
		}

		LIST_INSERT_HEAD(site->class_list, head, class, node);

		log_info("find class:%s url:%s", class->second_class.str, url.str);
	}

	/*
	   fix_class_info(site, page);
	   if(load_class_id(class) == MRT_ERR)
	   {
	   log_error("%s load class id error.", __func__);
	   }
	 */
	string_free(&part);
	string_free(&url);
	string_free(&caption);

	return MRT_SUC;
}

int fetch_dispatch(cw_site_t * site)
{
	cw_class_t *class = NULL;
	html_page_t page;
	string_t url;

	s_zero(url);
	s_zero(page);

	LIST_FOREACH(class, node, site->class_list, head) {
		if (string_copy(&url, &class->url) == MRT_ERR) {
			log_error("string_copys error:%s", get_error());
			return -1;
		}

		printf("will fetch class first:%s second:%s\n", class->first_class.str, class->second_class.str);
		//log_info("will fetch class first:%s second:%s", class->first_class.str, class->second_class.str);

		while (!http_recv_page(url.str, &page, 1)) {
			//这里找到了分类的一个页面
			log_info("recv page url:%s", url.str);

			if (html_fix_charset(&page, "utf8") == MRT_ERR) {
				log_error("change page charset to UTF-8 error, url:%s", url.str);
				http_free_page(&page);
				break;
			}
			//把这个页面里的文章url全部找出来
			if (fetch_article_url(site, class, &page) == MRT_ERR) {
				log_error("fetch content url error, url:%s", url.str);
				http_free_page(&page);
				break;
			}
			//把找出来的文章收集回来存入数据库中
			if (fetch_article_content(site) == MRT_ERR) {
				log_error("fetch_article_content error.");
			}
			//找到分类的下一页
			if (fetch_next_page(site, &page, &url) == MRT_ERR) {
				log_error("current page no found next page, url:%s", url.str);
				http_free_page(&page);
				break;
			}

			log_info("next page:%s", url.str);
			http_free_page(&page);
		}

		printf("fetch class over first:%s second:%s\n", class->first_class.str, class->second_class.str);
		//log_info("fetch class over first:%s second:%s", class->first_class.str, class->second_class.str);
	}

	return MRT_SUC;
}

int check_web(cw_site_t * site)
{
	html_page_t page;
	site->content_list = M_alloc(sizeof(cw_content_list_t));
	site->class_list = M_alloc(sizeof(cw_class_list_t));

	s_zero(page);

	LIST_INIT(site->content_list, head);
	LIST_INIT(site->class_list, head);

	M_ciril(http_recv_page(site->url, &page, 1), "get web data error, site:%s, url:%s.", site->name, site->url);
	strcpy(site->charset, "UTF-8");
	if (html_fix_charset(&page, "UTF-8") == MRT_ERR) {
		log_error("change page charset to UTF-8 error, url:%s", site->url);
		http_free_page(&page);
		return MRT_ERR;
	}

	/*
	   if(html_mark_filter(site->filter, &page.data) == MRT_ERR)
	   {
	   log_error("html_mark_filter error.");
	   return MRT_ERR;
	   }
	 */

	/* 临时不检测
	   if(check_update(site, &page))
	   {
	   log_info("current site was not updated, site:[%s], url:[%s].", site->name, site->url);
	   return MRT_ERR;
	   }
	 */

	//只对笑话进行采集
	//M_ciril(fetch_joke_list(site), "fetch joke list error, site:[%s], url:[%s]", site->name, site->url);

	//直接返回了！！！！！！
	//return MRT_SUC;

	M_ciril(fetch_class(site, &page), "get class error, site:[%s], url:[%s].", site->name, site->url);

	M_ciril(fetch_dispatch(site), "get content list error, site:[%s], url:[%s].", site->name, site->url);

	// M_ciril(fetch_preview_info(site), "get content info error, site:[%s], url:[%s].", site->name, site->url);

	log_info("%s check web site success,  site:[%s], url:[%s].", __func__, site->name, site->url);

	http_free_page(&page);

	return MRT_OK;
}

#ifdef TEST_FETCH

S_config server;

int main(int argc, char *argv[])
{
	cw_content_t content;

	if (log_init("./", "cwind", "debug") == MRT_ERR) {
		printf("%s init logger error.\n", __func__);
		return MRT_ERR;
	}

	if (memory_pool_init() == MRT_ERR) {
		printf("%s:%d mem pool init error.\n", __func__, __LINE__);
		return MRT_ERR;
	}

	if (load_config() == MRT_ERR) {
		printf("%s load config error.\n", __func__);
		return MRT_ERR;
	}

	strcpy(content.url_real.str, "http://www.4399.com/flash/217_1.htm?1024");

	strcpy(content.caption, "test");

	fetch_swf(&content);

	return 0;
}

#endif