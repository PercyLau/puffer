from django.conf.urls import url
from django.views.generic import TemplateView

urlpatterns = [
    url('^$', TemplateView.as_view(template_name='index.html')), #TODO: Replace with index.html
    url('index_dev.html', TemplateView.as_view(template_name='index_dev.html')),
]
