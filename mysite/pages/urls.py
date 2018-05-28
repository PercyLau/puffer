from django.conf.urls import url
from django.views.generic import TemplateView

urlpatterns = [
    url('^$', TemplateView.as_view(template_name='index.html')), #TODO: Replace with index.html
#    url('index.html', TemplateView.as_view(template_name='index.html')),
#    url('player.html', TemplateView.as_view(template_name='player.html')),
    url('index_dev.html', TemplateView.as_view(template_name='index_dev.html')),
#    url('widget.html', TemplateView.as_view(template_name='widget.html')),
#    url('index.js', TemplateView.as_view(template_name='index.js')),
#    url('player.js', TemplateView.as_view(template_name='player.js')),
#    url('config.js', TemplateView.as_view(template_name='config.js')),
#    url('puffer.js', TemplateView.as_view(template_name='puffer.js')),
#    url('style.css', TemplateView.as_view(template_name='style.css')),
]
